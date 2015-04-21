#include <bts/chain/proposal_evaluator.hpp>
#include <bts/chain/proposal_object.hpp>
#include <bts/chain/account_object.hpp>
#include <bts/chain/key_object.hpp>

namespace bts { namespace chain {

object_id_type proposal_create_evaluator::do_evaluate(const proposal_create_operation& o)
{
   const database& d = db();
   const auto& global_parameters = d.get_global_properties().parameters;

   auto bts_fee_required = o.calculate_fee(d.current_fee_schedule());
   auto bts_fee_paid = pay_fee(o.fee_paying_account, o.fee);
   FC_ASSERT( bts_fee_paid >= bts_fee_required );

   FC_ASSERT( o.expiration_time > d.head_block_time(), "Proposal has already expired on creation." );
   FC_ASSERT( o.expiration_time <= d.head_block_time() + global_parameters.maximum_proposal_lifetime,
              "Proposal expiration time is too far in the future.");
   FC_ASSERT( !o.review_period_seconds || fc::seconds(*o.review_period_seconds) < (o.expiration_time - d.head_block_time()),
              "Proposal review period must be less than its overall lifetime." );

   {
      // If we're dealing with the genesis authority, make sure this transaction has a sufficient review period.
      flat_set<account_id_type> auths;
      for( auto& op : o.proposed_ops )
         op.op.visit(operation_get_required_auths(auths, auths));
      if( auths.find(account_id_type()) != auths.end() )
         FC_ASSERT( o.review_period_seconds
                    && *o.review_period_seconds >= global_parameters.genesis_proposal_review_period );
   }

   for( const op_wrapper& op : o.proposed_ops )
      _proposed_trx.operations.push_back(op.op);
   _proposed_trx.validate();

   return object_id_type();
}

object_id_type proposal_create_evaluator::do_apply(const proposal_create_operation& o)
{
   database& d = db();

   const proposal_object& proposal = d.create<proposal_object>([&](proposal_object& proposal) {
      proposal.proposed_transaction = _proposed_trx;
      proposal.expiration_time = o.expiration_time;
      if( o.review_period_seconds )
         proposal.review_period_time = o.expiration_time - *o.review_period_seconds;

      //Populate the required approval sets
      flat_set<account_id_type> required_active;
      for( const operation& op : proposal.proposed_transaction.operations )
         op.visit(operation_get_required_auths(required_active, proposal.required_owner_approvals));
      //All accounts which must provide both owner and active authority should be omitted from the active authority set;
      //owner authority approval implies active authority approval.
      std::set_difference(required_active.begin(), required_active.end(),
                          proposal.required_owner_approvals.begin(), proposal.required_owner_approvals.end(),
                          std::inserter(proposal.required_active_approvals, proposal.required_active_approvals.begin()));
   });

   return proposal.id;
}

object_id_type proposal_update_evaluator::do_evaluate(const proposal_update_operation& o)
{
   database& d = db();

   auto bts_fee_required = o.calculate_fee(d.current_fee_schedule());
   auto bts_fee_paid = pay_fee(o.fee_paying_account, o.fee);
   FC_ASSERT( bts_fee_paid >= bts_fee_required );

   _proposal = &o.proposal(d);

   if( _proposal->review_period_time && d.head_block_time() >= *_proposal->review_period_time )
      FC_ASSERT( o.active_approvals_to_add.empty() && o.owner_approvals_to_add.empty(),
                 "This proposal is in its review period. No new approvals may be added." );

   for( account_id_type id : o.active_approvals_to_remove )
      FC_ASSERT( _proposal->available_active_approvals.find(id) != _proposal->available_active_approvals.end(),
                 "", ("id", id)("available", _proposal->available_active_approvals) );
   for( account_id_type id : o.owner_approvals_to_remove )
      FC_ASSERT( _proposal->available_owner_approvals.find(id) != _proposal->available_owner_approvals.end(),
                 "", ("id", id)("available", _proposal->available_owner_approvals) );
   for( key_id_type id : o.key_approvals_to_add )
      FC_ASSERT( trx_state->signed_by.find(id(d).key_address()) != trx_state->signed_by.end()
                 || trx_state->_skip_signature_check );
   for( key_id_type id : o.key_approvals_to_remove )
      FC_ASSERT( trx_state->signed_by.find(id(d).key_address()) != trx_state->signed_by.end()
                 || trx_state->_skip_signature_check );

   return object_id_type();
}

object_id_type proposal_update_evaluator::do_apply(const proposal_update_operation& o)
{
   database& d = db();

   apply_delta_balances();
   apply_delta_fee_pools();

   // Potential optimization: if _executed_proposal is true, we can skip the modify step and make push_proposal skip
   // signature checks. This isn't done now because I just wrote all the proposals code, and I'm not yet 100% sure the
   // required approvals are sufficient to authorize the transaction.
   d.modify(*_proposal, [&o, &d](proposal_object& p) {
      p.available_active_approvals.insert(o.active_approvals_to_add.begin(), o.active_approvals_to_add.end());
      p.available_owner_approvals.insert(o.owner_approvals_to_add.begin(), o.owner_approvals_to_add.end());
      for( account_id_type id : o.active_approvals_to_remove )
         p.available_active_approvals.erase(id);
      for( account_id_type id : o.owner_approvals_to_remove )
         p.available_owner_approvals.erase(id);
      for( key_id_type id : o.key_approvals_to_add )
         p.available_key_approvals.insert(id(d).key_address());
      for( key_id_type id : o.key_approvals_to_remove )
         p.available_key_approvals.erase(id(d).key_address());
   });

   // If the proposal has a review period, don't bother attempting to authorize/execute it.
   // Proposals with a review period may never be executed except at their expiration.
   if( _proposal->review_period_time )
      return object_id_type();

   if( _proposal->is_authorized_to_execute(&d) )
   {
      // All required approvals are satisfied. Execute!
      _executed_proposal = true;
      try {
         _processed_transaction = d.push_proposal(*_proposal);
      } catch(fc::exception& e) {
         wlog("Proposed transaction ${id} failed to apply once approved with exception:\n----\n${reason}\n----\nWill try again when it expires.",
              ("id", o.proposal)("reason", e.to_detail_string()));
         _proposal_failed = true;
      }
   }

   return object_id_type();
}

object_id_type proposal_delete_evaluator::do_evaluate(const proposal_delete_operation& o)
{
   database& d = db();

   auto bts_fee_required = o.calculate_fee(d.current_fee_schedule());
   auto bts_fee_paid = pay_fee(o.fee_paying_account, o.fee);
   FC_ASSERT( bts_fee_paid >= bts_fee_required );

   _proposal = &o.proposal(d);

   auto required_approvals = o.using_owner_authority? &_proposal->required_owner_approvals
                                                    : &_proposal->required_active_approvals;
   FC_ASSERT( required_approvals->find(o.fee_paying_account) != required_approvals->end(),
              "Provided authority is not authoritative for this proposal." );

   return object_id_type();
}

object_id_type proposal_delete_evaluator::do_apply(const proposal_delete_operation&)
{
   apply_delta_balances();
   apply_delta_fee_pools();

   db().remove(*_proposal);

   return object_id_type();
}

} } // bts::chain

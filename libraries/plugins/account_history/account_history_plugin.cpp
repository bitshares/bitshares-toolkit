#include <bts/account_history/account_history_plugin.hpp>

#include <bts/chain/time.hpp>
#include <bts/chain/operation_history_object.hpp>
#include <bts/chain/account_object.hpp>

#include <fc/thread/thread.hpp>

namespace bts { namespace account_history {

/**
 * @brief Used to calculate fees in a polymorphic manner
 *
 * If you wish to pay fees in an asset other than CORE, use the core_exchange_rate argument to specify the rate of
 * conversion you wish to use. The operation's fee will be calculated by multiplying the CORE fee by the provided
 * exchange rate. It is up to the caller to ensure that the core_exchange_rate converts to an asset accepted by the
 * delegates at a rate which they will accept.
 */
struct operation_get_impacted_accounts
{
   const operation_history_object& _op_history;
   const account_history_plugin&   _plugin;
   flat_set<account_id_type>&      _impacted;
   operation_get_impacted_accounts( const operation_history_object& oho, const account_history_plugin& ahp, flat_set<account_id_type>& impact )
      :_op_history(oho),_plugin(ahp),_impacted(impact)
   {}
   typedef void result_type;

   void add_authority( const authority& a )const
   {
      for( auto& item : a.auths )
      {
         if( item.first.type() == account_object_type )
            _impacted.insert( item.first );
      }
   }

   void operator()( const transfer_operation& o )const {
      _impacted.insert( o.to );
   }

   void operator()( const limit_order_create_operation& o )const {
   }

   void operator()( const short_order_create_operation& o )const {

   }

   void operator()( const limit_order_cancel_operation& o )const {

   }

   void operator()( const short_order_cancel_operation& o )const {

   }

   void operator()( const call_order_update_operation& o )const {

   }

   void operator()( const key_create_operation& o )const {

   }

   void operator()( const account_create_operation& o )const {
      _impacted.insert( _op_history.result.get<object_id_type>() ); 
   }

   void operator()( const account_update_operation& o )const {
      if( o.owner )
      {
         add_authority( *o.owner );
      }
      if( o.active )
      {
         add_authority( *o.active );
      }
   }

   void operator()( const account_whitelist_operation& o )const {
       _impacted.insert( o.account_to_list );
   }

   void operator()( const asset_create_operation& o )const {
   }

   void operator()( const asset_update_operation& o )const {
   }

   void operator()( const asset_issue_operation& o )const {
       _impacted.insert( o.issue_to_account );
   }

   void operator()( const asset_fund_fee_pool_operation& o )const {

   }

   void operator()( const delegate_publish_feeds_operation& o )const {

   }

   void operator()( const delegate_create_operation& o )const {

   }

   void operator()( const witness_withdraw_pay_operation& o )const {

   }

   void operator()( const proposal_create_operation& o )const {
       for( auto op : o.proposed_ops )
          op.op.visit( operation_get_required_auths( _impacted, _impacted ) );
   }

   void operator()( const proposal_update_operation& o )const {

   }

   void operator()( const proposal_delete_operation& o )const {

   }

   void operator()( const fill_order_operation& o )const {
      _impacted.insert( o.account_id );

   }
};



void account_history_plugin::configure(const account_history_plugin::plugin_config& cfg)
{
   _config = cfg;
   database().applied_block.connect( [&]( const signed_block& b){ update_account_histories(b); } );
   database().add_index< primary_index< simple_index< operation_history_object > > >();
   database().add_index< primary_index< simple_index< account_transaction_history_object > > >();
}

void account_history_plugin::update_account_histories( const signed_block& b )
{
   chain::database& db = database();
   const vector<operation_history_object>& hist = db.get_applied_operations();
   for( auto op : hist )
   {
      // add to the operation history index
      const auto& oho = db.create<operation_history_object>( [&]( operation_history_object& h ){
                                h = op;
                        });

      // get the set of accounts this operation applies to
      flat_set<account_id_type> impacted;
      op.op.visit( operation_get_required_auths( impacted, impacted ) );
      op.op.visit( operation_get_impacted_accounts( oho, *this, impacted ) );

      // for each operation this account applies to that is in the config link it into the history
      if( _config.accounts.size() == 0 )
      {
         for( auto& account_id : impacted )
         {
            // add history
            const auto& bal_obj = account_id(db).balances(db);
            const auto& ath = db.create<account_transaction_history_object>( [&]( account_transaction_history_object& obj ){
                obj.operation_id = oho.id;
                obj.next = bal_obj.most_recent_op;
            });
            db.modify( bal_obj, [&]( account_balance_object& obj ){
                obj.most_recent_op = ath.id;
            });
         }
      }
      else
      {
         for( auto account_id : _config.accounts )
         {
            if( impacted.find( account_id ) != impacted.end() )
            {
               // add history
               const auto& bal_obj = account_id(db).balances(db);
               const auto& ath = db.create<account_transaction_history_object>( [&]( account_transaction_history_object& obj ){
                   obj.operation_id = oho.id;
                   obj.next = bal_obj.most_recent_op;
               });
               db.modify( bal_obj, [&]( account_balance_object& obj ){
                   obj.most_recent_op = ath.id;
               });
            }
         }
      }
   }
}
} }

#pragma once
#include <bts/chain/operations.hpp>
#include <bts/chain/authority.hpp>
#include <bts/chain/asset.hpp>

namespace bts { namespace chain {
   class database;
   struct transaction;

   /**
    *  Place holder for state tracked while processing a
    *  transaction.  This class provides helper methods
    *  that are common to many different operations and
    *  also tracks which keys have signed the transaction
    */
   class transaction_evaluation_state
   {
      public:
         transaction_evaluation_state( database* db = nullptr, bool skip_sig_check = false )
         :_db(db),_skip_signature_check(skip_sig_check){}

         bool check_authority( const account_object*, authority::classification auth_class = authority::active, int depth = 0 );

         database& db()const { FC_ASSERT( _db ); return *_db; }

         /** derived from signatures on transaction */
         flat_set<address>                                          signed_by;
         /** cached approval (accounts and keys) */
         flat_set< pair<object_id_type,authority::classification> > approved_by;

         /**
          * Used to lookup new objects using transaction relative IDs
          */
         vector<operation_result>   operation_results;

         transaction* _trx;
         database*    _db = nullptr;
         bool         _skip_signature_check = false;
   };
} } // namespace bts::chain

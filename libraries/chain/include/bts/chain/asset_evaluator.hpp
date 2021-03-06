#pragma once
#include <bts/chain/evaluator.hpp>
#include <bts/chain/operations.hpp>
#include <bts/chain/database.hpp>

namespace bts { namespace chain {

   class asset_create_evaluator : public evaluator<asset_create_evaluator>
   {
      public:
         typedef asset_create_operation operation_type;

         object_id_type do_evaluate( const asset_create_operation& o );
         object_id_type do_apply( const asset_create_operation& o );
   };

   class asset_issue_evaluator : public evaluator<asset_issue_evaluator>
   {
      public:
         typedef asset_issue_operation operation_type;
         object_id_type do_evaluate( const asset_issue_operation& o );
         object_id_type do_apply( const asset_issue_operation& o );

         const asset_dynamic_data_object* asset_dyn_data = nullptr;
         const account_object*            to_account = nullptr;
   };

   class asset_burn_evaluator : public evaluator<asset_burn_evaluator>
   {
      public:
         typedef asset_burn_operation operation_type;
         object_id_type do_evaluate( const asset_burn_operation& o );
         object_id_type do_apply( const asset_burn_operation& o );

         const asset_dynamic_data_object* asset_dyn_data = nullptr;
         const account_object*            from_account = nullptr;
   };


   class asset_update_evaluator : public evaluator<asset_update_evaluator>
   {
      public:
         typedef asset_update_operation operation_type;

         object_id_type do_evaluate( const asset_update_operation& o );
         object_id_type do_apply( const asset_update_operation& o );

         const asset_object* asset_to_update = nullptr;
   };

   class asset_update_bitasset_evaluator : public evaluator<asset_update_bitasset_evaluator>
   {
      public:
         typedef asset_update_bitasset_operation operation_type;

         object_id_type do_evaluate( const asset_update_bitasset_operation& o );
         object_id_type do_apply( const asset_update_bitasset_operation& o );

         const asset_bitasset_data_object* bitasset_to_update = nullptr;
   };

   class asset_update_feed_producers_evaluator : public evaluator<asset_update_feed_producers_evaluator>
   {
      public:
         typedef asset_update_feed_producers_operation operation_type;

         object_id_type do_evaluate( const operation_type& o );
         object_id_type do_apply( const operation_type& o );

         const asset_bitasset_data_object* bitasset_to_update = nullptr;
   };

   class asset_fund_fee_pool_evaluator : public evaluator<asset_fund_fee_pool_evaluator>
   {
      public:
         typedef asset_fund_fee_pool_operation operation_type;

         object_id_type do_evaluate(const asset_fund_fee_pool_operation& op);
         object_id_type do_apply(const asset_fund_fee_pool_operation& op);

         const asset_dynamic_data_object* asset_dyn_data = nullptr;
   };

   class asset_global_settle_evaluator : public evaluator<asset_global_settle_evaluator>
   {
      public:
         typedef asset_global_settle_operation operation_type;

         object_id_type do_evaluate(const operation_type& op);
         object_id_type do_apply(const operation_type& op);

         const asset_object* asset_to_settle = nullptr;
   };
   class asset_settle_evaluator : public evaluator<asset_settle_evaluator>
   {
      public:
         typedef asset_settle_operation operation_type;

         object_id_type do_evaluate(const operation_type& op);
         object_id_type do_apply(const operation_type& op);

         const asset_object* asset_to_settle = nullptr;
   };

   class asset_publish_feeds_evaluator : public evaluator<asset_publish_feeds_evaluator>
   {
      public:
         typedef asset_publish_feed_operation operation_type;

         object_id_type do_evaluate( const asset_publish_feed_operation& o );
         object_id_type do_apply( const asset_publish_feed_operation& o );

         std::map<std::pair<asset_id_type,asset_id_type>,price_feed> median_feed_values;
   };

} } // bts::chain

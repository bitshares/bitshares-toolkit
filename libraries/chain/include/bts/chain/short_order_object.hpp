#pragma once 
#include <bts/chain/object.hpp>
#include <bts/chain/database.hpp>
#include <bts/chain/authority.hpp>
#include <bts/chain/asset.hpp>
#include <bts/chain/generic_index.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace bts { namespace chain {

  /**
   * @class short_order_object
   * @brief maintains state about requests to short an asset
   *
   * Short orders are only valid if their sell price is above the
   * fair market value of the asset at the feed price.  Users can
   * place shorts at any price but their order will be ignored
   * beyond the feed.
   *
   * All shorts have a minimial initial collateral ratio requirement that is
   * defined by the network, but individuals may choose to have a higher
   * initial collateral to avoid the risk of being margin called. 
   *
   * All shorts have a maitenance collateral ratio that must be kept or
   * the network will automatically cover the short order.  Users can
   * specify a higher maitenance collateral ratio as a form of "stop loss"
   * and to potentially get ahead of a short squeeze.  
   */
  class short_order_object : public abstract_object<short_order_object>
  {
     public:
        static const uint8_t space_id = protocol_ids;
        static const uint8_t type_id  = short_order_object_type;

        account_id_type  seller;   
        share_type       available_collateral; ///< asset_id == sell_price.quote.asset_id
        price            sell_price; ///< the price the short is currently at = min(limit_price,feed)
        uint16_t         initial_collateral_ratio    = 0; ///< may be higher than the network requires
        uint16_t         maitenance_collateral_ratio = 0; ///< may optionally be higher than the network requires

        asset amount_for_sale(uint16_t network_init_collateral_ratio)const;
        price call_price(uint16_t network_maitenance_collateral_ratio)const;
  };

  class call_order_object : public abstract_object<call_order_object>
  {
     public:
        static const uint8_t space_id = protocol_ids;
        static const uint8_t type_id  = call_order_object_type;

        asset get_collateral()const { return asset( collateral, call_price.quote.asset_id ); }
        asset get_debt()const { return asset( collateral, call_price.base.asset_id ); }

        account_id_type  borrower;   
        share_type       collateral;  ///< call_price.quote.asset_id, access via get_collateral
        share_type       debt;        ///< call_price.base.asset_id, access via get_collateral
        price            call_price;
  };

  struct by_id;
  struct by_price;
  typedef multi_index_container< 
     short_order_object,
     indexed_by<  
        hashed_unique< tag<by_id>, 
           member< object, object_id_type, &object::id > >,
        ordered_unique< tag<by_price>, 
           composite_key< short_order_object, 
              member< short_order_object, price, &short_order_object::sell_price>,
              member< object, object_id_type, &object::id>
           >
        >
     >
  > short_order_multi_index_type;

  typedef multi_index_container< 
     call_order_object,
     indexed_by<  
        hashed_unique< tag<by_id>, 
           member< object, object_id_type, &object::id > >,
        ordered_unique< tag<by_price>, 
           composite_key< call_order_object, 
              member< call_order_object, price, &call_order_object::call_price>,
              member< object, object_id_type, &object::id>
           >
        >
     >
  > call_order_multi_index_type;


  typedef generic_index<short_order_object, short_order_multi_index_type> short_order_index;
  typedef generic_index<call_order_object, call_order_multi_index_type>   call_order_index;


} } // bts::chain 

FC_REFLECT_DERIVED( bts::chain::short_order_object, (bts::chain::object), 
                    (seller)(available_collateral)(sell_price)(initial_collateral_ratio)(maitenance_collateral_ratio) 
                  )

FC_REFLECT_DERIVED( bts::chain::call_order_object, (bts::chain::object),
                    (borrower)(collateral)(debt)(call_price) )

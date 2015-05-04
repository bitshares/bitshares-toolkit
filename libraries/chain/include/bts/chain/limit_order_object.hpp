#pragma once
#include <bts/db/object.hpp>
#include <bts/chain/authority.hpp>
#include <bts/chain/asset.hpp>
#include <bts/db/generic_index.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace bts { namespace chain {
  using namespace bts::db;

  /**
   *  @brief an offer to sell a amount of a asset at a specified exchange rate by a certain time
   *  @ingroup object
   *  @ingroup protocol
   *  @ingroup market 
   *
   *  This limit_order_objects are indexed by @ref expiration and is automatically deleted on the first block after expiration. 
   */
  class limit_order_object : public abstract_object<limit_order_object>
  {
     public:
        static const uint8_t space_id = protocol_ids;
        static const uint8_t type_id  = limit_order_object_type;

        time_point_sec   expiration;
        account_id_type  seller;
        share_type       for_sale; ///< asset id is sell_price.base.asset_id
        price            sell_price;

        asset amount_for_sale()const   { return asset( for_sale, sell_price.base.asset_id ); }
        asset amount_to_receive()const { return amount_for_sale() * sell_price; }
  };

  struct by_id;
  struct by_price;
  struct by_expiration;
  typedef multi_index_container<
     limit_order_object,
     indexed_by<
        hashed_unique< tag<by_id>,
           member< object, object_id_type, &object::id > >,
        ordered_non_unique< tag<by_expiration>, member< limit_order_object, time_point_sec, &limit_order_object::expiration> >,
        ordered_unique< tag<by_price>,
           composite_key< limit_order_object,
              member< limit_order_object, price, &limit_order_object::sell_price>,
              member< object, object_id_type, &object::id>
           >,
           composite_key_compare< std::greater<price>, std::less<object_id_type> >
        >
     >
  > limit_order_multi_index_type;

  typedef generic_index<limit_order_object, limit_order_multi_index_type> limit_order_index;

} }

FC_REFLECT_DERIVED( bts::chain::limit_order_object,
                    (bts::db::object),
                    (expiration)(seller)(for_sale)(sell_price)
                  )


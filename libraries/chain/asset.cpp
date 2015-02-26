#include <bts/chain/asset.hpp>
#include <boost/rational.hpp>

namespace bts { namespace chain {
      bool operator < ( const price& a, const price& b )
      {
         if( a.base.asset_id < b.base.asset_id ) return true;
         if( a.base.asset_id > b.base.asset_id ) return false;
         if( a.quote.asset_id < b.quote.asset_id ) return true;
         if( a.quote.asset_id > b.quote.asset_id ) return false;

         return boost::rational<uint64_t>(a.quote.amount.value,a.base.amount.value) 
                <
                boost::rational<uint64_t>(b.quote.amount.value,b.base.amount.value);
      }
} } // bts::chain

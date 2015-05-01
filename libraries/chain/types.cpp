#include <bts/chain/config.hpp>
#include <bts/chain/types.hpp>

#include <fc/crypto/base58.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/exception/exception.hpp>
#include <fc/io/raw.hpp>

namespace bts { namespace chain {

    public_key_type::public_key_type():key_data(){};

    public_key_type::public_key_type( const fc::ecc::public_key_data& data )
        :key_data( data ) {};

    public_key_type::public_key_type( const fc::ecc::public_key& pubkey )
        :key_data( pubkey ) {};

    public_key_type::public_key_type( const std::string& base58str )
    {
      // TODO:  Refactor syntactic checks into static is_valid()
      //        to make public_key_type API more similar to address API
       std::string prefix( BTS_ADDRESS_PREFIX );
       const size_t prefix_len = prefix.size();
       FC_ASSERT( base58str.size() > prefix_len );
       FC_ASSERT( base58str.substr( 0, prefix_len ) ==  prefix , "", ("base58str", base58str) );
       auto bin = fc::from_base58( base58str.substr( prefix_len ) );
       auto bin_key = fc::raw::unpack<binary_key>(bin);
       key_data = bin_key.data;
       FC_ASSERT( fc::ripemd160::hash( key_data.data, key_data.size() )._hash[0] == bin_key.check );
    };

    public_key_type::operator fc::ecc::public_key_data() const
    {
       return key_data;
    };

    public_key_type::operator fc::ecc::public_key() const
    {
       return fc::ecc::public_key( key_data );
    };

    public_key_type::operator std::string() const
    {
       binary_key k;
       k.data = key_data;
       k.check = fc::ripemd160::hash( k.data.data, k.data.size() )._hash[0];
       auto data = fc::raw::pack( k );
       return BTS_ADDRESS_PREFIX + fc::to_base58( data.data(), data.size() );
    }

    bool operator == ( const public_key_type& p1, const fc::ecc::public_key& p2)
    {
       return p1.key_data == p2.serialize();
    }

    bool operator == ( const public_key_type& p1, const public_key_type& p2)
    {
       return p1.key_data == p2.key_data;
    }

    bool operator != ( const public_key_type& p1, const public_key_type& p2)
    {
       return p1.key_data != p2.key_data;
    }

} } // bts::chain

namespace fc
{
    using namespace std;
    void to_variant( const bts::chain::public_key_type& var,  fc::variant& vo )
    {
        vo = std::string(var);
    }

    void from_variant( const fc::variant& var,  bts::chain::public_key_type& vo )
    {
        vo = bts::chain::public_key_type( var.as_string() );
    }
    void to_variant( const bts::chain::fee_schedule_type& var,  fc::variant& vo )
    {
       vector<pair<bts::chain::fee_type,uint32_t> > fees;
       fees.reserve(var.size());
       for( uint32_t i = 0; i < var.size(); ++i )
          fees.push_back( std::make_pair( bts::chain::fee_type(i), var.fees.at(i) ) );
       vo = variant( fees );
    }
    void from_variant( const fc::variant& var,  bts::chain::fee_schedule_type& vo )
    {
       vo = bts::chain::fee_schedule_type();
       auto fees = var.as<vector<pair<bts::chain::fee_type,uint32_t>>>();
       for( auto item :  fees )
          vo.set( item.first, item.second );
    }

    void to_variant(const bts::chain::vote_id_type& var, variant& vo)
    {
       vo = string(var);
    }
    void from_variant(const variant& var, bts::chain::vote_id_type& vo)
    {
       vo = bts::chain::vote_id_type(var.as_string());
    }

} // fc

#include <bts/chain/address.hpp>
#include <bts/chain/types.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/base58.hpp>
#include <algorithm>

namespace bts {
  namespace chain {
   address::address(){}

   address::address( const std::string& base58str )
   {
      FC_ASSERT( is_valid( base58str ) );
      std::string prefix( BTS_ADDRESS_PREFIX );
      std::vector<char> v = fc::from_base58( base58str.substr( prefix.size() ) );
      memcpy( (char*)addr._hash, v.data(), std::min<size_t>( v.size()-4, sizeof( addr ) ) );
   }

   bool address::is_valid( const std::string& base58str, const std::string& prefix )
   {
      const size_t prefix_len = prefix.size();
      if( base58str.size() <= prefix_len )
          return false;
      if( base58str.substr( 0, prefix_len ) != prefix )
          return false;
      std::vector<char> v;
      try
      {
		  v = fc::from_base58( base58str.substr( prefix_len ) );
	  }
	  catch( const fc::parse_error_exception& e )
	  {
		  return false;
	  }
      if( v.size() != sizeof( fc::ripemd160 ) + 4 )
          return false;
      const fc::ripemd160 checksum = fc::ripemd160::hash( v.data(), v.size() - 4 );
      if( memcmp( v.data() + 20, (char*)checksum._hash, 4 ) != 0 )
          return false;
      return true;
   }

   address::address( const fc::ecc::public_key& pub )
   {
       auto dat = pub.serialize();
       addr = fc::ripemd160::hash( fc::sha512::hash( dat.data, sizeof( dat ) ) );
   }

   address::address( const pts_address& ptsaddr )
   {
       addr = fc::ripemd160::hash( (char*)&ptsaddr, sizeof( ptsaddr ) );
   }

   address::address( const fc::ecc::public_key_data& pub )
   {
       addr = fc::ripemd160::hash( fc::sha512::hash( pub.data, sizeof( pub ) ) );
   }

   address::address( const bts::chain::public_key_type& pub )
   {
       addr = fc::ripemd160::hash( fc::sha512::hash( pub.key_data.data, sizeof( pub.key_data ) ) );
   }

   address::operator std::string()const
   {
        fc::array<char,24> bin_addr;
        memcpy( (char*)&bin_addr, (char*)&addr, sizeof( addr ) );
        auto checksum = fc::ripemd160::hash( (char*)&addr, sizeof( addr ) );
        memcpy( ((char*)&bin_addr)+20, (char*)&checksum._hash[0], 4 );
        return BTS_ADDRESS_PREFIX + fc::to_base58( bin_addr.data, sizeof( bin_addr ) );
   }

} } // namespace bts::chain

namespace fc
{
    void to_variant( const bts::chain::address& var,  variant& vo )
    {
        vo = std::string(var);
    }
    void from_variant( const variant& var,  bts::chain::address& vo )
    {
        vo = bts::chain::address( var.as_string() );
    }
}

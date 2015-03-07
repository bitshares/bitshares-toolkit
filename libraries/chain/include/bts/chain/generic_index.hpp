#pragma once
#include <bts/chain/index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

namespace bts { namespace chain {

   using boost::multi_index_container;
   using namespace boost::multi_index;

   struct by_id{};
   /**
    *  Almost all objects can be tracked and managed via a boost::multi_index container that uses
    *  an unordered_unique key on the object ID.  This templace class adapts the generic index interface
    *  to work with arbitrary boost multi_index containers on the same type.
    */
   template<typename ObjectType, typename MultiIndexType>
   class generic_index : public index 
   {
      public:
         typedef MultiIndexType index_type;
         typedef ObjectType     object_type;

         virtual const object& insert( object&& obj ) 
         {
            assert( nullptr != dynamic_cast<ObjectType*>(&obj) );
            auto insert_result = indicies.insert( std::move( static_cast<ObjectType&>(obj) ) );
            FC_ASSERT( insert_result.second, "Could not insert object, most likely a uniqueness constraint was violated" );
            return *insert_result.first;
         }

         virtual const object&  create(const std::function<void(object&)>& constructor )
         {
            ObjectType item;
            item.id = get_next_id();
            constructor( item );
            auto insert_result = indicies.insert( std::move(item) );
            FC_ASSERT(insert_result.second, "Could not create object! Most likely a uniqueness constraint is violated.");
            use_next_id();
            return *insert_result.first;
         }

         virtual void modify( const object& obj, const std::function<void(object&)>& m )override
         {
            assert( nullptr != dynamic_cast<const ObjectType*>(&obj) );
            auto ok = indicies.modify( indicies.iterator_to( static_cast<const ObjectType&>(obj) ), 
                                       [&m]( ObjectType& o ){ m(o); } );
            FC_ASSERT( ok, "Could not modify object, most likely a index constraint was violated" );
         }

         virtual void remove( const object& obj )override
         {
            indicies.erase( indicies.iterator_to( static_cast<const ObjectType&>(obj) ) );
         }

         virtual const object* find( object_id_type id )const override
         {
            auto itr = indicies.find( id );
            if( itr == indicies.end() ) return nullptr;
            return &*itr;
         }

         virtual void inspect_all_objects(std::function<void (const object&)> inspector) override
         {
            try {
               for( const auto& ptr : indicies )
                  inspector(ptr);
            } FC_CAPTURE_AND_RETHROW()
         }

         index_type indicies;
   };

} }

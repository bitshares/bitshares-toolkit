
#pragma once
#include <bts/chain/index.hpp>
#include <bts/chain/asset_object.hpp>

namespace bts { namespace chain {
    
   /**
    *  @class asset_index 
    *  @brief enable asset lookup by NAME or INSTANCE and enforce uniqueness
    */
   class asset_index : public index
   {
      public:
         typedef asset_object object_type;

         ~asset_index(){}

         virtual object_id_type get_next_available_id()const override;
         virtual packed_object  get_meta_object()const override;
         virtual void           set_meta_object( const packed_object& obj ) override;

         /**
          * Builds a new object and assigns it the next available ID and then
          * initializes it with constructor and lastly inserts it into the index.
          */
         virtual const object*  create( const std::function<void(object*)>& constructor );

         virtual int64_t size()const;

         virtual void modify( const object* obj, const std::function<void(object*)>& m )override;
         virtual void add( unique_ptr<object> o )override;
         virtual void remove( object_id_type id )override;
         virtual const object* get( object_id_type id )const override;

         const asset_object* get( const string& symbol )const;

      private:
         vector<unique_ptr<asset_object> >    assets;
         unordered_map<string,asset_object*>  symbol_to_id;
   };

} } 

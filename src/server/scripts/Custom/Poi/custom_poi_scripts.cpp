/*

 * NobleNext POI — world scripts (startup).

 * Full POI list is pulled by NobleNext client addon on load via `.poi sync`.

 * Original POI system by ERINGAR.

 */



#include "custom_poi_handler.h"

#include "ScriptMgr.h"



namespace RoleplayCore

{

    class poi_world_script : public WorldScript

    {

    public:

        poi_world_script() : WorldScript("poi_world_script") { }



        void OnStartup() override

        {

            PoiHandler::Instance().Initialize();

        }

    };

}



void AddSC_CustomPoiScripts()

{

    new RoleplayCore::poi_world_script();

}


/*
 * Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _RoleplayDATABASE_H
#define _RoleplayDATABASE_H

#include "MySQLConnection.h"

enum RoleplayDatabaseStatements
{
    /*  Naming standard for defines:
        {DB}_{SEL/INS/UPD/DEL/REP}_{Summary of data changed}
        When updating more than one field, consider looking at the calling function
        name for a suiting suffix.
    */
    // SELECTS
    Roleplay_SEL_CREATUREEXTRA,
    Roleplay_SEL_CREATUREEXTRA_TEMPLATE,
    Roleplay_SEL_GAMEOBJECTEXTRA_BY_CREATOR,
    Roleplay_SEL_GAMEOBJECTEXTRA_BY_GUID,
    Roleplay_SEL_GAMEOBJECTEXTRA_BY_BNET,
    Roleplay_SEL_SERVER_SETTINGS,
    Roleplay_SEL_RP_PHASES,
    Roleplay_SEL_RP_PHASE_MEMBERS,
    Roleplay_SEL_CHARACTER_RP_PHASES,
    Roleplay_SEL_CHARACTER_RP_PHASE_BY_CHARACTER,
    Roleplay_SEL_RP_PHASE_SPAWNS,
    Roleplay_SEL_RP_PHASE_SPAWN_BY_KEY,
    Roleplay_SEL_RP_PHASE_SPAWN_COUNT_BY_KEY,
    Roleplay_SEL_RP_PHASE_LATEST_BY_META,
    Roleplay_SEL_RP_PHASE_ADDON_DATA,
    Roleplay_SEL_RP_PHASE_ADDON_DATA_KEY,

    // DELETIONS
    Roleplay_DEL_CREATUREEXTRA,
    Roleplay_DEL_GAMEOBJECTEXTRA,
    Roleplay_DEL_CUSTOMNPC,
    Roleplay_DEL_CUSTOMNPCOWNER,
    Roleplay_DEL_SERVER_SETTINGS,
    Roleplay_DEL_RP_PHASE_MEMBER,
    Roleplay_DEL_CHARACTER_RP_PHASE,
    Roleplay_DEL_RP_PHASE_SPAWN,

    // UPDATES
    Roleplay_UPD_CREATUREEXTRA_TEMPLATE,
    Roleplay_UPD_RP_PHASE,
    Roleplay_UPD_RP_PHASE_NAME,
    Roleplay_UPD_RP_PHASE_ARCHIVE,
    Roleplay_UPD_RP_PHASE_UNARCHIVE,
    Roleplay_UPD_RP_PHASE_PUBLIC,
    Roleplay_UPD_RP_PHASE_OWNER,
    Roleplay_UPD_RP_PHASE_ENTER_SPAWN,
    Roleplay_UPD_RP_PHASE_ADDON_DATA_CAS,

    // REPLACES
    Roleplay_REP_CREATUREEXTRA,
    Roleplay_REP_GAMEOBJECTEXTRA,
    Roleplay_REP_CUSTOMNPCDATA,
    Roleplay_REP_CUSTOMNPCOWNER,
    Roleplay_REP_SERVER_SETTINGS,
    Roleplay_REP_CHARACTER_RP_PHASE,

    // INSERTS
    Roleplay_INS_RP_PHASE,
    Roleplay_INS_RP_PHASE_MEMBER,
    Roleplay_INS_RP_PHASE_SPAWN,
    Roleplay_INS_RP_PHASE_ADDON_DATA,
    Roleplay_INS_RP_PHASE_AUDIT,

    MAX_RoleplayDATABASE_STATEMENTS
};

class TC_DATABASE_API RoleplayDatabaseConnection : public MySQLConnection
{
public:
    typedef RoleplayDatabaseStatements Statements;

    RoleplayDatabaseConnection(MySQLConnectionInfo& connInfo, ConnectionFlags connectionFlags);
    ~RoleplayDatabaseConnection();

    //- Loads database type specific prepared statements
    void DoPrepareStatements() override;
};

#endif

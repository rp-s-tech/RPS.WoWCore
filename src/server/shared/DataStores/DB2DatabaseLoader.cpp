/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
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

#include "DB2DatabaseLoader.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "DB2Meta.h"
#include "Errors.h"
#include "Log.h"
#include <cstring>

static char const* nullStr = "";

char* DB2DatabaseLoader::Load(bool custom, uint32& records, char**& indexTable, std::vector<char*>& stringPool, uint32& minId)
{
    // Even though this query is executed only once, prepared statement is used to send data from mysql server in binary format
    HotfixDatabasePreparedStatement* stmt = HotfixDatabase.GetPreparedStatement(_loadInfo->Statement);
    stmt->setBool(0, !custom);
    PreparedQueryResult result = HotfixDatabase.Query(stmt);
    if (!result)
        return nullptr;

    if (_loadInfo->Meta->GetDbFieldCount() != result->GetFieldCount())
        return nullptr;

    // get struct size and index pos
    uint32 indexField = _loadInfo->Meta->GetDbIndexField();
    uint32 recordSize = _loadInfo->Meta->GetRecordSize();

    // we store flat holders pool as single memory block
    std::size_t stringFields = _loadInfo->GetStringFieldCount(false);

    // Resize index table
    uint32 indexTableSize = records;
    if (PreparedQueryResult maxIdResult = HotfixDatabase.Query(HotfixDatabase.GetPreparedStatement(HotfixDatabaseStatements(_loadInfo->Statement + HOTFIX_MAX_ID_STMT_OFFSET))))
        if (uint32((*maxIdResult)[0].GetUInt64()) > records)
            indexTableSize = uint32((*maxIdResult)[0].GetUInt64());

    if (indexTableSize > records)
    {
        char** tmpIdxTable = new char*[indexTableSize];
        memset(tmpIdxTable, 0, indexTableSize * sizeof(char*));
        memcpy(tmpIdxTable, indexTable, records * sizeof(char*));
        delete[] indexTable;
        indexTable = tmpIdxTable;
    }

    char* tempDataTable = new char[result->GetRowCount() * recordSize];
    memset(tempDataTable, 0, result->GetRowCount() * recordSize);
    uint32* newIndexes = new uint32[result->GetRowCount()];
    if (stringFields)
        stringPool.reserve(std::max<uint64>(stringPool.capacity(), stringPool.size() + stringFields * result->GetRowCount() + 1));

    std::size_t newRecords = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32 offset = 0;

        uint32 indexValue = fields[indexField].GetUInt32();
        bool isNew = false;

        // Attempt to overwrite existing data
        char* dataValue = indexTable[indexValue];
        if (!dataValue)
        {
            newIndexes[newRecords] = indexValue;
            dataValue = &tempDataTable[newRecords++ * recordSize];
            isNew = true;
        }

        uint32 f = 0;
        if (!_loadInfo->Meta->HasIndexFieldInData())
        {
            *((uint32*)(&dataValue[offset])) = indexValue;
            offset += 4;
            ++f;
        }

        for (uint32 x = 0; x < _loadInfo->Meta->FieldCount; ++x)
        {
            for (uint32 z = 0; z < _loadInfo->Meta->Fields[x].ArraySize; ++z)
            {
                switch (_loadInfo->Fields[f].Type)
                {
                    case FT_FLOAT:
                        *((float*)(&dataValue[offset])) = fields[f].GetFloat();
                        offset += 4;
                        break;
                    case FT_INT:
                        *((int32*)(&dataValue[offset])) = fields[f].GetInt32();
                        offset += 4;
                        break;
                    case FT_BYTE:
                        *((int8*)(&dataValue[offset])) = fields[f].GetInt8();
                        offset += 1;
                        break;
                    case FT_SHORT:
                        *((int16*)(&dataValue[offset])) = fields[f].GetInt16();
                        offset += 2;
                        break;
                    case FT_LONG:
                        *((int64*)(&dataValue[offset])) = fields[f].GetInt64();
                        offset += 8;
                        break;
                    case FT_STRING:
                    {
                        LocalizedString* slot = (LocalizedString*)(&dataValue[offset]);
                        if (isNew)
                            for (char const*& localeStr : slot->Str)
                                localeStr = nullStr;

                        // Value in database in main table field must be for enUS locale
                        if (char* str = AddString(&slot->Str[LOCALE_enUS], fields[f].GetStringView()))
                            stringPool.push_back(str);

                        offset += sizeof(LocalizedString);
                        break;
                    }
                    case FT_STRING_NOT_LOCALIZED:
                    {
                        char const** slot = (char const**)(&dataValue[offset]);

                        // Value in database in main table field must be for enUS locale
                        if (char* str = AddString(slot, fields[f].GetStringView()))
                            stringPool.push_back(str);
                        else
                            *slot = nullStr;

                        offset += sizeof(char*);
                        break;
                    }
                    default:
                        ABORT_MSG("Unknown format character '%c' found in " STRING_VIEW_FMT " meta for field %s",
                            _loadInfo->Fields[f].Type, STRING_VIEW_FMT_ARG(_storageName), _loadInfo->Fields[f].Name);
                        break;
                }
                ++f;
            }
        }

        ASSERT(offset == recordSize);
    } while (result->NextRow());

    if (!newRecords)
    {
        delete[] tempDataTable;
        delete[] newIndexes;
        return nullptr;
    }

    // Compact new data table to only contain new records not previously loaded from file
    char* dataTable = new char[newRecords * recordSize];
    memcpy(dataTable, tempDataTable, newRecords * recordSize);

    // insert new records to index table
    for (std::size_t i = 0; i < newRecords; ++i)
    {
        uint32 newId = newIndexes[i];
        indexTable[newId] = &dataTable[i * recordSize];
        minId = std::min(minId, newId);
    }

    delete[] tempDataTable;
    delete[] newIndexes;

    records = indexTableSize;

    return dataTable;
}

void DB2DatabaseLoader::LoadStrings(bool custom, LocaleConstant locale, uint32 records, char** indexTable, std::vector<char*>& stringPool)
{
    HotfixDatabasePreparedStatement* stmt = HotfixDatabase.GetPreparedStatement(HotfixDatabaseStatements(_loadInfo->Statement + HOTFIX_LOCALE_STMT_OFFSET));
    stmt->setBool(0, !custom);
    stmt->setString(1, std::string_view(localeNames[locale]));
    PreparedQueryResult result = HotfixDatabase.Query(stmt);
    if (!result)
        return;

    std::size_t stringFields = _loadInfo->GetStringFieldCount(true);
    if (result->GetFieldCount() != stringFields + 1 /*ID*/)
        return;

    uint32 fieldCount = _loadInfo->Meta->FieldCount;
    uint32 recordSize = _loadInfo->Meta->GetRecordSize();

    stringPool.reserve(std::max<uint64>(stringPool.capacity(), stringPool.size() + stringFields * result->GetRowCount() + 1));

    do
    {
        Field* fields = result->Fetch();
        uint32 offset = 0;
        uint32 stringFieldNumInRecord = 0;
        uint32 indexValue = fields[0].GetUInt32();

        if (indexValue >= records)
            continue;

        // Attempt to overwrite existing data
        if (char* dataValue = indexTable[indexValue])
        {
            uint32 fieldIndex = 0;
            if (!_loadInfo->Meta->HasIndexFieldInData())
            {
                offset += 4;
                ++fieldIndex;
            }

            for (uint32 x = 0; x < fieldCount; ++x)
            {
                for (uint32 z = 0; z < _loadInfo->Meta->Fields[x].ArraySize; ++z)
                {
                    switch (_loadInfo->Fields[fieldIndex].Type)
                    {
                        case FT_FLOAT:
                        case FT_INT:
                            offset += 4;
                            break;
                        case FT_BYTE:
                            offset += 1;
                            break;
                        case FT_SHORT:
                            offset += 2;
                            break;
                        case FT_LONG:
                            offset += 8;
                            break;
                        case FT_STRING:
                        {
                            // fill only not filled entries
                            LocalizedString* db2str = (LocalizedString*)(&dataValue[offset]);
                            if (char* str = AddString(&db2str->Str[locale], fields[1 + stringFieldNumInRecord].GetStringView()))
                                stringPool.push_back(str);

                            ++stringFieldNumInRecord;
                            offset += sizeof(LocalizedString);
                            break;
                        }
                        case FT_STRING_NOT_LOCALIZED:
                            offset += sizeof(char*);
                            break;
                        default:
                            ABORT_MSG("Unknown format character '%c' found in " STRING_VIEW_FMT " meta for field %s",
                                _loadInfo->Fields[fieldIndex].Type, STRING_VIEW_FMT_ARG(_storageName), _loadInfo->Fields[fieldIndex].Name);
                            break;
                    }
                    ++fieldIndex;
                }
            }

            ASSERT(offset == recordSize);
        }
        else
            TC_LOG_ERROR("sql.sql", "Hotfix locale table for storage {} references row that does not exist {} locale {}!", _storageName, indexValue, localeNames[locale]);

    } while (result->NextRow());
}

// NobleNext: per-record version of Load() + LoadStrings(), keep the field loop in sync with Load()
// Text query on purpose: no new prepared statements per table. Rows go verified first, custom last, the last row wins (same as Load(false) then Load(true)).
char* DB2DatabaseLoader::LoadRecord(uint32 id, std::string_view tableName, char const* oldRecord, std::vector<char*>& stringPool, bool& schemaError)
{
    schemaError = false;
    uint32 dbFieldCount = _loadInfo->Meta->GetDbFieldCount();
    if (_loadInfo->FieldCount != dbFieldCount)
    {
        TC_LOG_ERROR("sql.sql", "DB2DatabaseLoader::LoadRecord: storage {} has {} load info fields, meta expects {}", _storageName, _loadInfo->FieldCount, dbFieldCount);
        schemaError = true;
        return nullptr;
    }

    std::string columns;
    std::string localeColumns;
    for (uint32 i = 0; i < dbFieldCount; ++i)
    {
        if (!columns.empty())
            columns += ", ";
        columns += Trinity::StringFormat("`{}`", _loadInfo->Fields[i].Name);

        if (_loadInfo->Fields[i].Type == FT_STRING)
            localeColumns += Trinity::StringFormat(", `{}_lang`", _loadInfo->Fields[i].Name);
    }

    QueryResult result = HotfixDatabase.Query(Trinity::StringFormat("SELECT {} FROM `{}` WHERE `ID` = {} ORDER BY (`VerifiedBuild` > 0) DESC",
        columns, tableName, id).c_str());
    if (!result)
    {
        // Query() also returns nullptr on an SQL error (unknown column, already logged): tell it apart from a missing row
        schemaError = bool(HotfixDatabase.Query(Trinity::StringFormat("SELECT 1 FROM `{}` WHERE `ID` = {} LIMIT 1", tableName, id).c_str()));
        return nullptr;
    }

    if (result->GetFieldCount() != dbFieldCount)
    {
        TC_LOG_ERROR("sql.sql", "DB2DatabaseLoader::LoadRecord: table `{}` returned {} columns, storage {} expects {}", tableName, result->GetFieldCount(), _storageName, dbFieldCount);
        schemaError = true;
        return nullptr;
    }

    uint32 recordSize = _loadInfo->Meta->GetRecordSize();

    // Always a new block: the old record stays valid for raw pointers (ItemTemplate, indexes) and is never written to
    char* record = new char[recordSize];
    if (oldRecord)
        memcpy(record, oldRecord, recordSize); // keeps strings of locales that have no row in the locale table
    else
        memset(record, 0, recordSize);

    bool isNew = !oldRecord;
    std::vector<uint32> localizedOffsets; // FT_STRING slots in field order, same order as the `_lang` columns

    do
    {
        Field* fields = result->Fetch();
        uint32 offset = 0;
        localizedOffsets.clear();

        uint32 f = 0;
        if (!_loadInfo->Meta->HasIndexFieldInData())
        {
            *((uint32*)(&record[offset])) = id;
            offset += 4;
            ++f;
        }

        for (uint32 x = 0; x < _loadInfo->Meta->FieldCount; ++x)
        {
            for (uint32 z = 0; z < _loadInfo->Meta->Fields[x].ArraySize; ++z)
            {
                switch (_loadInfo->Fields[f].Type)
                {
                    case FT_FLOAT:
                        *((float*)(&record[offset])) = fields[f].GetFloat();
                        offset += 4;
                        break;
                    case FT_INT:
                        *((int32*)(&record[offset])) = fields[f].GetInt32();
                        offset += 4;
                        break;
                    case FT_BYTE:
                        *((int8*)(&record[offset])) = fields[f].GetInt8();
                        offset += 1;
                        break;
                    case FT_SHORT:
                        *((int16*)(&record[offset])) = fields[f].GetInt16();
                        offset += 2;
                        break;
                    case FT_LONG:
                        *((int64*)(&record[offset])) = fields[f].GetInt64();
                        offset += 8;
                        break;
                    case FT_STRING:
                    {
                        LocalizedString* slot = (LocalizedString*)(&record[offset]);
                        if (isNew)
                            for (char const*& localeStr : slot->Str)
                                localeStr = nullStr;

                        // Unlike Load(), an empty enUS value really clears the string: the row is the whole truth for this record
                        if (char* str = AddString(&slot->Str[LOCALE_enUS], fields[f].GetStringView()))
                            stringPool.push_back(str);
                        else
                            slot->Str[LOCALE_enUS] = nullStr;

                        localizedOffsets.push_back(offset);
                        offset += sizeof(LocalizedString);
                        break;
                    }
                    case FT_STRING_NOT_LOCALIZED:
                    {
                        char const** slot = (char const**)(&record[offset]);
                        if (char* str = AddString(slot, fields[f].GetStringView()))
                            stringPool.push_back(str);
                        else
                            *slot = nullStr;

                        offset += sizeof(char*);
                        break;
                    }
                    default:
                        ABORT_MSG("Unknown format character '%c' found in " STRING_VIEW_FMT " meta for field %s",
                            _loadInfo->Fields[f].Type, STRING_VIEW_FMT_ARG(_storageName), _loadInfo->Fields[f].Name);
                        break;
                }
                ++f;
            }
        }

        ASSERT(offset == recordSize);
        isNew = false;
    } while (result->NextRow());

    if (localizedOffsets.empty())
        return record;

    QueryResult localeResult = HotfixDatabase.Query(Trinity::StringFormat("SELECT `locale`{} FROM `{}_locale` WHERE `ID` = {} ORDER BY (`VerifiedBuild` > 0) DESC",
        localeColumns, tableName, id).c_str());
    if (!localeResult)
        return record;

    if (localeResult->GetFieldCount() != localizedOffsets.size() + 1 /*locale*/)
    {
        TC_LOG_ERROR("sql.sql", "DB2DatabaseLoader::LoadRecord: table `{}_locale` returned {} columns, storage {} expects {}", tableName, localeResult->GetFieldCount(), _storageName, localizedOffsets.size() + 1);
        return record;
    }

    do
    {
        Field* fields = localeResult->Fetch();
        LocaleConstant locale = GetLocaleByName(fields[0].GetStringView());
        if (!IsValidLocale(locale) || locale == LOCALE_enUS)
            continue;

        // Same as LoadStrings(): an empty locale value keeps the previous string
        for (std::size_t i = 0; i < localizedOffsets.size(); ++i)
            if (char* str = AddString(&((LocalizedString*)(&record[localizedOffsets[i]]))->Str[locale], fields[1 + i].GetStringView()))
                stringPool.push_back(str);
    } while (localeResult->NextRow());

    return record;
}

char* DB2DatabaseLoader::AddString(char const** holder, std::string_view value)
{
    if (!value.empty())
    {
        char* str = new char[value.length() + 1];
        memcpy(str, value.data(), value.length());
        str[value.length()] = '\0';
        *holder = str;
        return str;
    }

    return nullptr;
}

/*
 * Copyright (C) 2005-2013 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define _CRT_SECURE_NO_DEPRECATE

#include <stdio.h>
#include <deque>
#include <map>
#include <set>
#include <cstdlib>

#ifdef WIN32
#include "direct.h"
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "dbcfile.h"

#include "loadlib/adt.h"
#include "loadlib/wdt.h"
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <stdio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <conio.h>
#include <sstream>
#include <vector>
#include <map>
#include <fstream>

#if defined( __GNUC__ )
    #define _open   open
    #define _close    close
    #ifndef O_BINARY
        #define O_BINARY 0
    #endif
#else
    #include <io.h>
#endif

#ifdef O_LARGEFILE
    #define OPEN_FLAGS  (O_RDONLY | O_BINARY | O_LARGEFILE)
#else
    #define OPEN_FLAGS (O_RDONLY | O_BINARY)
#endif

using namespace std;

typedef struct
{
    char name[64];
    uint32 id;
} map_id;

map_id *map_ids;
uint16 *areas;
uint16 *LiqType;
char output_path[128] = ".";
char input_path[128] = ".";
uint32 maxAreaId = 0;
int CONF_max_build = 0;

//**************************************************
// Extractor options
//**************************************************
enum Extract
{
    EXTRACT_MAP = 1,
    EXTRACT_DBC = 2
};

// Select data for extract
int   CONF_extract = EXTRACT_MAP | EXTRACT_DBC;
// This option allow limit minimum height to some value (Allow save some memory)
// see contrib/mmap/src/Tilebuilder.h, INVALID_MAP_LIQ_HEIGHT
bool  CONF_allow_height_limit = true;
float CONF_use_minHeight = -500.0f;

// This option allow use float to int conversion
bool  CONF_allow_float_to_int   = true;
float CONF_float_to_int8_limit  = 2.0f;      // Max accuracy = val/256
float CONF_float_to_int16_limit = 2048.0f;   // Max accuracy = val/65536
float CONF_flat_height_delta_limit = 0.005f; // If max - min less this value - surface is flat
float CONF_flat_liquid_delta_limit = 0.001f; // If max - min less this value - liquid surface is flat
bool  CONF_generate_sql_files = false;		 // Generate SQL files from DBC Files
bool  CONF_generate_csv_files = false;		 // Generate CSV files from DBC Files
bool  CONF_remove_dbc = false;				 // Remove DBC after SQL Generation
bool  CONF_create_xml_file = false;			 // Build an XML config file based on DBC file contents

//TODO: Moved into the XML file
static char* const langs[] = {"enGB", "enUS", "deDE", "esES", "frFR", "koKR", "zhCN", "zhTW", "enCN", "enTW", "esMX", "ruRU" };

int LANG_COUNT = 12;                        //TODO: Moved into the XML file

//TODO: Move this into the XML file
int MIN_SUPPORTED_BUILD = 15050;                           // code expect mpq files and mpq content files structure for this build or later
uint32 CURRENT_BUILD = 0;                    //TODO: Moved into the XML file
int EXPANSION_COUNT = 4;                     //TODO: Moved into the XML file
int WORLD_COUNT = 1;                         //TODO: Moved into the XML file
int FIRST_LOCALE=-1;

class Reader
{
    public:
        Reader();
        ~Reader();
        bool LoadBinary(char *, string, int);
        bool LoadWDB(char *, string, unsigned char *, int);
        //bool LoadADB_DBC_DB2(char *, string, unsigned char *, int);
        bool LoadADB_DBC_DB2_predicted(char *, unsigned char *);
        void ExtractBinaryInfo(string fileName);
        void WriteSqlStructure(ofstream& fileRef,string& filename);
        void WriteSqlData(ofstream& fileRef,string& filename);
        void GenerateXml(ofstream& fileRef,string& filename);
        template<typename T> string ToStr(T);
        void InitializeBoolFielTypes(int);
    private:
        int totalRecords, totalFields, recordSize, stringSize;
        vector<vector<unsigned char *>> recordData;
        unsigned char *stringData;
        bool *isStringField, *isFloatField, *isIntField, *isByteField, *isBoolField;
        bool isADB, isDB2, isDBC, isWDB, isWDBItem;
};

Reader::Reader()
{
    totalFields = 0;
    totalRecords = 0;
    recordSize = 0;
    stringSize = 0;

    recordData.clear();

    stringData = 0;

    isStringField = 0;
    isFloatField = 0;
    isIntField = 0;
    isByteField = 0;
    isBoolField = 0;

    isADB = false;
    isDB2 = false;
    isDBC = false;
    isWDB = false;

    isWDBItem = false;
}

Reader::~Reader()
{
    totalFields = 0;
    totalRecords = 0;
    recordSize = 0;
    stringSize = 0;

    recordData.clear();

    if (stringData)
        delete[] stringData;

    if (isStringField)
        delete[] isStringField;

    if (isFloatField)
        delete[] isFloatField;

    if (isIntField)
        delete[] isIntField;

    if (isByteField)
        delete[] isByteField;

    if (isBoolField)
        delete[] isBoolField;

    isADB = false;
    isDB2 = false;
    isDBC = false;
    isWDB = false;

    isWDBItem = false;
}

void Reader::InitializeBoolFielTypes(int _fields)
{
    isStringField = new bool[_fields];
    isFloatField = new bool[_fields];
    isIntField = new bool[_fields];
    isByteField = new bool[_fields];
    isBoolField = new bool[_fields];

    for (int currentField = 0; currentField < _fields; currentField++)
    {
        isStringField[currentField] = false;
        isFloatField[currentField] = false;
        isIntField[currentField] = false;
        isByteField[currentField] = false;
        isBoolField[currentField] = false;
    }
}

template<typename T> string Reader::ToStr(T i)
{
    ostringstream buffer;

    buffer << i;

    return buffer.str();
}

bool Reader::LoadBinary(char *fileName, string fileFormat, int _recordSize)
{
    FILE *input = fopen(fileName, "rb");
    if(!input)
    {
        printf("\nERROR: Can't open file '%s'.\n", fileName);
        return false;
    }

    fseek(input, 0, SEEK_END);
    long fileSize = ftell(input);

    if (fileSize < 4)
    {
        printf("\nERROR: '%s': File size too small.\n", fileName);
        fclose(input);
        return false;
    }

    rewind(input);

    char headerName[4];
    fread(&headerName, 4, 1, input);

    if (headerName[0] == 'W' && headerName[1] == 'C' && headerName[2] == 'H' && headerName[3] == '2')
        isADB = true;
    else if (headerName[0] == 'W' && headerName[1] == 'D' && headerName[2] == 'B' && headerName[3] == '2')
        isDB2 = true;
    else if (headerName[0] == 'W' && headerName[1] == 'D' && headerName[2] == 'B' && headerName[3] == 'C')
        isDBC = true;
    else if (headerName[0] == 'B' && headerName[1] == 'D' && headerName[2] == 'I' && headerName[3] == 'W')        // BDIW itemcache.wdb
    {
        isWDBItem = true;
        isWDB = true;
    }
    else if ((headerName[0] == 'B' && headerName[1] == 'O' && headerName[2] == 'M' && headerName[3] == 'W') ||    // BOMW creaturecache.wdb
             (headerName[0] == 'B' && headerName[1] == 'O' && headerName[2] == 'G' && headerName[3] == 'W') ||    // BOGW gameobjectcache.wdb
             (headerName[0] == 'B' && headerName[1] == 'D' && headerName[2] == 'N' && headerName[3] == 'W') ||    // BDNW itemnamecache.wdb
             (headerName[0] == 'X' && headerName[1] == 'T' && headerName[2] == 'I' && headerName[3] == 'W') ||    // XTIW itemtextcache.wdb
             (headerName[0] == 'C' && headerName[1] == 'P' && headerName[2] == 'N' && headerName[3] == 'W') ||    // CPNW npccache.wdb
             (headerName[0] == 'X' && headerName[1] == 'T' && headerName[2] == 'P' && headerName[3] == 'W') ||    // XTPW pagetextcache.wdb
             (headerName[0] == 'T' && headerName[1] == 'S' && headerName[2] == 'Q' && headerName[3] == 'W')       // TSQW questcache.wdb
            )
        isWDB = true;
    else
    {
        printf("\nERROR: '%s': Unknown file type.\n", fileName);
        fclose(input);
        return false;
    }

    // WDB Parse
    if (isWDB)
    {
        // 24 bytes del header + 8 bytes del primer record y su el tama�o del record
        if (fileSize < 32)
        {
            printf("\nERROR: '%s': WDB structure is damaged.\n", fileName);
            fclose(input);
            return false;
        }

        // char Header[4];    // actualmente ya esta leido arriba para comprobar el tipo de archivo
        int WDBRevision = 0;
        char WDBLocale[4];
        int WDBMaxRecordSize = 0;
        int unk1 = 0;
        int unk2 = 0;
        // WDBEntry
        // WDBRecordSize

        fread(&WDBRevision, 4, 1, input);
        fread(&WDBLocale, 4, 1, input);
        fread(&WDBMaxRecordSize, 4, 1, input);
        fread(&unk1, 4, 1, input);
        fread(&unk2, 4, 1, input);

        string _tempWDBLocale = ToStr(WDBLocale[3]) + ToStr(WDBLocale[2]) + ToStr(WDBLocale[1]) + ToStr(WDBLocale[0]);

        long WDBDataSize = fileSize - 24;
        unsigned char *WDBData = new unsigned char[WDBDataSize];
        fread(WDBData, WDBDataSize, 1, input);
        fclose(input);

        if (LoadWDB(fileName, fileFormat, WDBData, WDBDataSize))
            printf("WDB file loaded: '%s' (Revision: %i, Locale: %s)\n", fileName, WDBRevision, _tempWDBLocale.c_str());
        else
            return false;
    }
    else if (isDBC || isADB || isDB2)
    {
        if (fileSize < 20)
        {
            printf("\nERROR: '%s': Unknown file format.\n", fileName);
            fclose(input);
            return false;
        }

        fread(&totalRecords, 4, 1, input);
        fread(&totalFields, 4, 1, input);
        fread(&recordSize, 4, 1, input);
        fread(&stringSize, 4, 1, input);

        if (!totalRecords || !totalFields || !recordSize)
        {
            printf("\nERROR: '%s': No records/fields found in file.\n", fileName);
            fclose(input);
            return false;
        }

        int headerSize = 20;
        long unkBytes = fileSize - headerSize - (totalRecords * recordSize) - stringSize;
        long dataBytes = fileSize - headerSize - unkBytes - stringSize;
        long stringBytes = fileSize - headerSize - unkBytes - (totalRecords * recordSize);
        if ((totalRecords < 0) || (totalFields < 0) || (stringSize < 0) ||
            (dataBytes < 0) || (stringBytes < 0) ||
            (dataBytes != (totalRecords * recordSize)) || (stringBytes != stringSize))
        {
            printf("\nERROR: '%s': Structure is damaged.\n", fileName);
            fclose(input);
            return false;
        }

        if (unkBytes)
        {
            unsigned char *unkData = new unsigned char[unkBytes];
            fread(unkData, unkBytes, 1, input);
        }

        unsigned char *dataData = new unsigned char[dataBytes];
        fread(dataData, dataBytes, 1, input);

        if (stringBytes > 1)
        {
            stringData = new unsigned char[stringSize];
            fread(stringData, stringSize, 1, input);
        }

        fclose(input);

        char *_tempFileType = "";
        if (isDBC)
            _tempFileType = "DBC";
        else if (isADB)
            _tempFileType = "ADB";
        else
            _tempFileType = "DB2";

        if (!LoadADB_DBC_DB2_predicted(fileName, dataData))
            return false;
    }

    // por si las dudas
    if (input)
        fclose(input);

    return true;
}

bool Reader::LoadADB_DBC_DB2_predicted(char *fileName, unsigned char *data)
{
    if (recordSize/totalFields != 4)
    {
        if (recordSize % 4 != 0)
        {
            printf("\nERROR: '%s': Predicted: Not supported byte packed format.\n", fileName);
            return false;
        }
        totalFields = recordSize / 4;
    }

    // Esta funcion es necesaria, de lo contrario ocasionara crash
    InitializeBoolFielTypes(totalFields);

    int offset = 0;    // contador global usado para saber en que posicion debe leer el siguiente registro

    for (int currentRecord = 0; currentRecord < totalRecords; currentRecord++)
    {
        vector<unsigned char *> fieldData;
        for (int currentField = 0; currentField < totalFields; currentField++)
        {
            int fieldSize = 4;

            fieldData.push_back(data + offset);
            offset += fieldSize;
        }
        recordData.push_back(fieldData);
    }

    // Float Field System
    for (int currentField = 0; currentField < totalFields; currentField++)
    {
        int counterfloat = 0, counterint = 0;

        for (int currentRecord = 0; currentRecord < totalRecords; currentRecord++)
        {
            int value = *reinterpret_cast<int *>(recordData[currentRecord][currentField]);
            if (value)
            {
                float floatValue = *reinterpret_cast<float *>(recordData[currentRecord][currentField]);
                string floatString = ToStr(floatValue);
                int isFloat1 = floatString.find("e");
                int isFloat2 = floatString.find("#");
                if ((isFloat1 == -1) && (isFloat2 == -1))
                    counterfloat++;
                else
                    counterint++;
            }
        }

        if (counterfloat > counterint)
            isFloatField[currentField] = true;
        else
            isFloatField[currentField] = false;
    }

    // Bool Field System
    if (totalRecords > 3)
    {
        for (int currentField = 0; currentField < totalFields; currentField++)
        {
            if (!isFloatField[currentField])
            {
                bool isBool = true;

                for (int currentRecord = 0; currentRecord < totalRecords; currentRecord++)
                {
                    int value = *reinterpret_cast<int *>(recordData[currentRecord][currentField]);
                    if (value < 0 || value > 1)
                    {
                        isBool = false;
                        break;
                    }
                }

                if (isBool)
                    isBoolField[currentField] = true;
            }
        }
    }

    // String Field System
    if (stringSize > 1)
    {
        for (int currentField = 0; currentField < totalFields; currentField++)
        {
            if (!isFloatField[currentField] && !isBoolField[currentField])
            {
                for (int currentRecord = 0; currentRecord < totalRecords; currentRecord++)
                {
                    int value = *reinterpret_cast<int *>(recordData[currentRecord][currentField]);
                    if ((value < 0) || (value >= stringSize))
                    {
                        isStringField[currentField] = false;
                        break;
                    }
                    else if ((value > 0) && (stringData[value-1]))
                    {
                        isStringField[currentField] = false;
                        break;
                    }
                    else if ((value > 0) && !stringData[value-1])
                        isStringField[currentField] = true;
                }
            }
        }
    }

    // Int Field System
    for (int currentField = 0; currentField < totalFields; currentField++)
        if (!isFloatField[currentField] && !isStringField[currentField] && !isBoolField[currentField])
            isIntField[currentField] = true;

    // Byte Field false siempre para predicted
    for (int currentField = 0; currentField < totalFields; currentField++)
        isByteField[currentField] = false;

    return true;
}

bool Reader::LoadWDB(char *fileName, string format, unsigned char *data, int dataSize)
{
    // Declaraciones globales en clase, pero totalFields y totalRecords no son definitivas
    // hasta que el final de la funcion
    totalFields = format.size();    // aun le faltan 2, pero eso se hace hasta abajo de esta funcion
    totalRecords = 0;
    stringSize = 0;

    // Esta funcion es necesaria, de lo contrario ocasionara crash
    InitializeBoolFielTypes(totalFields + 1);

    long orignaldataSize = dataSize + 24;

    bool isFirstRecord = true;
    int offset = 0;    // contador global usado para saber en que posicion debe leer el siguiente registro

    // repetir desde aqui
    while (true)
    {
        int entry = 0;
        int recordSize = 0;

        if ((dataSize -= 8) >= 0)
        {
            entry = *reinterpret_cast<int *>(data + offset);
            offset += 4;
            recordSize = *reinterpret_cast<int *>(data + offset);
            offset += 4;

            if (isFirstRecord && (!entry || !recordSize))
            {
                printf("\nERROR: '%s': No records found.\n", fileName);
                return false;
            }
            else if (!isFirstRecord && (!entry || !recordSize))
                break;

            isFirstRecord = false;

            if ((dataSize -= recordSize) >= 0)
            {
                vector<unsigned char *> fieldData;

                // entry, se le restan 8 al offset, debido a que ya se le sumaron 8
                fieldData.push_back(data + (offset - 8));
                isIntField[0] = true;
                // StatsCount caso especial para itemcache.wdb
                bool CountItemStats = false;
                bool isCompletedStats = true;
                bool neverRepeat = true;
                int StatsCount = 0;
                int StatsRemaining = 20;

                for (int currentField = 0; currentField < totalFields; currentField++)
                {
                    if (CountItemStats && neverRepeat)
                    {
                        if (!StatsRemaining)
                        {
                            isCompletedStats = true;
                            neverRepeat = false;
                        }
                        else
                        {
                            isCompletedStats = false;
                            if (StatsCount)
                            {
                                isIntField[currentField + 1] = true;
                                fieldData.push_back(data + offset);
                                offset += 4;
                                recordSize -= 4;
                                StatsCount -= 1;
                                StatsRemaining -= 1;
                            }
                            else
                            {
                                isIntField[currentField + 1] = true;
                                unsigned char *_tempiii = new unsigned char[sizeof(int)];
                                _tempiii[0] = 0;
                                _tempiii[1] = 0;
                                _tempiii[2] = 0;
                                _tempiii[3] = 0;
                                fieldData.push_back(_tempiii);
                                StatsRemaining -= 1;
                            }
                        }
                    }

                    if (isCompletedStats)
                    {
                        switch(format[currentField])
                        {
                            case 'b':    // byte
                            case 'X':    // unk byte
                                //printf("%c, %i\n", format[currentField], *reinterpret_cast<char *>(data + offset));
                                isByteField[currentField + 1] = true;
                                fieldData.push_back(data + offset);
                                offset += 1;
                                recordSize -= 1;
                                break;
                            case 's':
                            {
                                string _tempString = reinterpret_cast<char *>(data + offset);
                                //printf("%c, %s\n", format[currentField], _tempString.c_str());
                                isStringField[currentField + 1] = true;
                                fieldData.push_back(data + offset);
                                offset += _tempString.size() + 1;
                                recordSize -= _tempString.size() + 1;
                                break;
                            }
                            case 'd':    // int
                            case 'n':    // int
                            case 'x':    // unk int
                            case 'i':    // int
                                //printf("%c, %i\n", format[currentField], *reinterpret_cast<int *>(data + offset));
                                isIntField[currentField + 1] = true;
                                if (isWDBItem && (format[currentField] == 'x') && !CountItemStats)
                                {
                                    StatsCount = *reinterpret_cast<int *>(data + offset);
                                    StatsCount *= 2;
                                    CountItemStats = true;
                                }
                                fieldData.push_back(data + offset);
                                offset += 4;
                                recordSize -= 4;
                                break;
                            case 'f':    // float
                                //printf("%c, %f\n", format[currentField], *reinterpret_cast<float *>(data + offset));
                                isFloatField[currentField + 1] = true;
                                fieldData.push_back(data + offset);
                                offset += 4;
                                recordSize -= 4;
                                break;
                        } // switch(format[currentField])
                    } // if (isCompletedStats)

                    if ((recordSize > 0) && ((currentField + 1) >= totalFields))
                    {
                        printf("\nERROR: '%s': You must read '%i' bytes more per record.\n", fileName, recordSize);
                        return false;
                    }
                    else if ((recordSize < 0) && ((currentField + 1) >= totalFields))
                    {
                        printf("\nERROR: '%s': Exceeded record size by '%i' bytes.\n", fileName, recordSize * -1);
                        return false;
                    }
                } // for (unsigned int currentField = 0; currentField < totalFields; currentField++)

                recordData.push_back(fieldData);
            } // if ((dataSize -= recordSize) >= 0)
            else
            {
                printf("\nERROR: '%s': Corrupted WDB file.\n", fileName);
                return false;
            }
        } // if ((dataSize -= 8) >= 0)
        else
        {
            printf("\nERROR: '%s': Unexpected End of file in WDB, expected file size '%li'.\n", fileName, orignaldataSize-dataSize);
            return false;
        }
    } // while (true)

    // Del total de fields de formato solo se agrega uno mas que es el entry
    totalFields += 1;
    totalRecords = recordData.size();

    return true;
}

void Reader::ExtractBinaryInfo(string fileName)
{
    string outputFileName = fileName + ".csv";
    FILE *output = fopen(outputFileName.c_str(), "w");
    if(!output)
    {
        printf("\nERROR: File cannot be created '%s'.\n", outputFileName.c_str());
        return;
    }

    for (int currentField = 0; currentField < totalFields; currentField++)
    {
        if (isStringField[currentField])
            fprintf(output, "string");
        else if (isFloatField[currentField])
            fprintf(output, "float");
        else if (isByteField[currentField])
            fprintf(output, "byte");
        else if (isIntField[currentField] || isBoolField[currentField])
            fprintf(output, "int");

        if (currentField+1 < totalFields)
            fprintf(output, ",");
    }
    fprintf(output, "\n");

    for (int currentRecord = 0; currentRecord < totalRecords; currentRecord++)
    {
        for (int currentField = 0; currentField < totalFields; currentField++)
        {
            if (!isWDB && (stringSize > 1) && isStringField[currentField])
            {
                int value = *reinterpret_cast<int *>(recordData[currentRecord][currentField]);
                if (value)
                {
                    string outText = "\"";
                    for (int x = value; x < stringSize; x++)
                    {
                        if (!stringData[x])
                            break;

                        if (stringData[x] == '"')
                            outText.append("\"");

                        if (stringData[x] == '\r')
                        {
                            outText.append("\\r");
                            continue;
                        }

                        if (stringData[x] == '\n')
                        {
                            outText.append("\\n");
                            continue;
                        }

                        outText.append(ToStr(stringData[x]));
                    }
                    outText.append("\"");
                    fprintf(output, "%s", outText.c_str());
                }
            }
            else if (isWDB && isStringField[currentField])
            {
                string _tempText = reinterpret_cast<char *>(recordData[currentRecord][currentField]);
                int value = _tempText.size();
                if (value)
                {
                    string outText = "\"";
                    for (int x = 0; x < value; x++)
                    {
                        if (!_tempText[x])
                            break;

                        if (_tempText[x] == '"')
                            outText.append("\"");

                        if (_tempText[x] == '\r')
                        {
                            outText.append("\\r");
                            continue;
                        }

                        if (_tempText[x] == '\n')
                        {
                            outText.append("\\n");
                            continue;
                        }

                        outText.append(ToStr(_tempText[x]));
                    }
                    outText.append("\"");
                    fprintf(output, "%s", outText.c_str());
                }
            }
            else if (isFloatField[currentField])
                fprintf(output, "%f", *reinterpret_cast<float *>(recordData[currentRecord][currentField]));
            else if (isByteField[currentField])
                fprintf(output, "%d", *reinterpret_cast<char *>(recordData[currentRecord][currentField]));
            else if (isIntField[currentField] || isBoolField[currentField])
                fprintf(output, "%i", *reinterpret_cast<int *>(recordData[currentRecord][currentField]));

            if (currentField+1 < totalFields)
                fprintf(output, ",");
        }
        fprintf(output, "\n");
    }
    fclose(output);
}

struct FileStructure
{
    string Structure;
    int recordSize;
};

map<string, FileStructure> mFileNames, mTempFileNames;
bool isConfig = true;

void CreateDir(const std::string& Path)
{
#ifdef WIN32
    _mkdir(Path.c_str());
#else
    mkdir(Path.c_str(), 0777);
#endif
}

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if(from.empty())
        return;
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

string CleanFilename(string Filename)
{
    string outFilename = Filename;
    replaceAll(outFilename,".dbc","");
    replaceAll(outFilename,"./dbc/","");
    replaceAll(outFilename,".db2","");
    replaceAll(outFilename,"./db2/","");

    return outFilename;
}

std::vector<std::string> LookupDBCinXML(string Filename)
{
    std::vector<std::string> outFieldname ;
    //printf("\n 1");
    bool foundKey = false;
    int FIELDCOUNT=0;
    int CURRENTFIELD = 0;

    ifstream xmlfile2 ("ad.xml");
    string thisLine;

    if (xmlfile2.is_open())
    {
        while ( xmlfile2.good() )
        {
            std::getline(xmlfile2,thisLine);
            string startTag = (string)"<" + Filename + ">";
            string endTag = (string)"</" + Filename + ">";
            if (thisLine.find(startTag.c_str()) != string::npos)
            {
            // Found the start Tag, need to build a list of the lines between this and the end tag
                foundKey = true;
                CURRENTFIELD = 0;
                
            }
            else if (thisLine.find(endTag.c_str()) != string::npos)
            {
                foundKey = false;
                //printf(" 2");
                break;
            // Found the end tag, need to bail out of this routine
            }
            else if (thisLine.find("<fieldcount>") != string::npos)
            {
                if (foundKey == true)
                {
                    //printf(" 3");

                    // Read the number of fields
                    string result=thisLine;
                    replaceAll(result,"fieldcount","");
                    replaceAll(result,"/","");
                    replaceAll(result,"<>","");
                    replaceAll(result," ","");

                    int numb;
                    istringstream ( result ) >> numb;
                    FIELDCOUNT = numb;
                
                    //printf("\nFieldCount=%u\n",FIELDCOUNT);
                    outFieldname.resize(FIELDCOUNT);
                    //printf(" 4");
                }
            }
            else if (thisLine.find("<field") != string::npos)
            {
                if (foundKey == true)
                {
//                    printf(" 5");
                    string result=thisLine;
                    replaceAll(result,"field type","");
                    replaceAll(result,"include=\"y\"","");
                    replaceAll(result,"include=\"n\"","");
                    replaceAll(result,"/","");
                    replaceAll(result,"<>","");
                    replaceAll(result," ","");
                    replaceAll(result,"<","");
                    replaceAll(result,">","");
                    replaceAll(result,"=\"float\"","");
                    replaceAll(result,"=\"bigint\"","");
                    replaceAll(result,"=\"text\"","");
                    replaceAll(result,"name=","");
                    replaceAll(result,"=","");
            
                    replaceAll(result,"\"","");

                    string thisline2;
                    istringstream ( result ) >> thisline2;

                    //printf("\nSize=%u",outFieldname.size());
                    //printf(" Current=%u\n",CURRENTFIELD);

                    outFieldname[CURRENTFIELD] = thisline2;

                    CURRENTFIELD +=1;
                    //printf(" 6");

                }
            }
        }
    }
    try
    {
    xmlfile2.close();
    }
    catch (exception e)
    {
    }
  //  printf(" 7");
    //for (int thisfield = 0; thisfield < outFieldname.size(); ++thisfield)
    //{
    //    printf("\nFieldName: %s",outFieldname[thisfield].c_str());
    //}
    return outFieldname;
}

void Reader::WriteSqlStructure(ofstream& fileRef,string& filename)
{
    // Generate the SQL Header Section
    fileRef << "DROP TABLE IF EXISTS `dbc_" + filename + "`;" << endl;
    fileRef << "CREATE TABLE `dbc_"+ filename +"` (" << endl;

    // Generate the SQL for the Fields
    int maxColumns = totalFields;
    std::vector<std::string>thisColumnName;
    //TODO: Check the totalFields matches the number listed in the config file for this file
    //      If it does, replace the Colxx column names with fieldnames from the config file
    int thisMaxColumns =0;
        try
        {
            thisMaxColumns = LookupDBCinXML(filename).size();
            if (thisMaxColumns>0)
            {
                thisColumnName.resize(thisMaxColumns);
                totalFields = thisMaxColumns;
                thisColumnName = LookupDBCinXML(filename);
            }
            else
            {
                thisColumnName.resize(0);
            }
        }
        catch (exception b)
        {
            thisMaxColumns = 0;
            thisColumnName.resize(0);
        }

    //printf ("\nStarting Loop");
    for (int currentField = 0; currentField < maxColumns; ++currentField)
    {
        // STAGE6 - Replace Col below with the data contained in name

        if (thisColumnName.size()>0)
        {
            try
            {
                if (thisColumnName[currentField].length() > 0)
                {
                    fileRef << "\t`";
                    fileRef << thisColumnName[currentField];
                    fileRef << "`" ;
                }
                else
                {
                    fileRef << "\t`Col";
                    fileRef << currentField;
                    fileRef << "`" ;
                }
            }
            catch (exception a)
            {
                    fileRef << "\t`Col";
                    fileRef << currentField;
                    fileRef << "`" ;
            }
        }
        else // thisColumnName.size() = 0
        {
                    fileRef << "\t`Col";
                    fileRef << currentField;
                    fileRef << "`" ;
        }
            //TODO: Override these settings with values from the Config File
            if (isStringField[currentField])
            {
                fileRef << " TEXT NOT NULL";
            }
            else if (isFloatField[currentField])
            {
                fileRef << " FLOAT NOT NULL DEFAULT '0'";
            }
            else if (isByteField[currentField])
            {
                fileRef << " TINYINT UNSIGNED NOT NULL DEFAULT '0'";
            }
            else if (isIntField[currentField] || isBoolField[currentField])
            {
               fileRef << " BIGINT NOT NULL DEFAULT '0'";
            }

            if (currentField+1 < totalFields)
                fileRef << "," << endl;
    }
    //TODO:     Build a list of the primary keys and add this to the SQL definition
    //{
    //    sqlWriter.WriteLine("\tPRIMARY KEY (`{0}`)", index.ColumnName);
    //}

    // Close off the final part of the header section
   fileRef << ")" << endl;
    fileRef << " ENGINE=MyISAM DEFAULT CHARSET=utf8 COLLATE=utf8_general_ci COMMENT='Export of " + filename + "';" << endl;
    fileRef << " SET NAMES UTF8;" << endl;
}

void Reader::GenerateXml(ofstream& fileRef,string& filename)
{
    // Generate the SQL Header Section
    fileRef << "        <" + filename + ">" << endl;
    fileRef << "            <include>Y</include>" << endl;
    fileRef << "            <tablename>dbc_" + filename + "</tablename>" << endl;
    fileRef << "            <fieldcount>" << totalFields << "</fieldcount>" << endl;

    // Generate the SQL for the Fields
    //TODO: Check the totalFields matches the number listed in the config file for this file
    //      If it does, replace the Colxx column names with fieldnames from the config file

    for (int currentField = 0; currentField < totalFields; ++currentField)
    {
        //TODO:     Check the config file to see whether this column should be included.

        fileRef << "            <field type=\"";

        //TODO: Override these settings with values from the Config File
        if (isStringField[currentField])
            fileRef << "text\"";
        else if (isFloatField[currentField])
            fileRef << "float\"";

        else if (isByteField[currentField])
            fileRef << "tinyint\"";

        else if (isIntField[currentField] || isBoolField[currentField])
            fileRef << "bigint\"";

//        if (currentField+1 < totalFields)
            fileRef << " name=\"col" << currentField << "\" include=\"y\" />" << endl;
    }
    fileRef << "        </" + filename + ">" << endl;
//    fileRef.close();
}

static string StripBadCharacters(string input)
{
    replaceAll(input,"'","\'");
    replaceAll(input,"""","\"""");

    return input;
}

void Reader::WriteSqlData(ofstream& fileRef,string& filename)
{
    int flds = 0;
    int maxColumns = totalFields;//data.Columns.Count

        //TODO: Retrieve column names from config.xml

        //1  <Files>
        //2   <Achievement>
        //3       <include>Y</include>
        //4       <tablename>dbc_Achievement</tablename>
        //5       <fieldcount>15</fieldcount>
        //6       <field type="bigint" name="id" include="y" />
        //7   </Achievement>

        // STAGE1 - Find a match for <filename> in the xml  //
        // STAGE2 - Find a match for </filename> in the xml //

        // STAGE3 - Check that fieldcount (from 5) matches maxColumns

        // STAGE4 - Read the line (6) into an array, include is the important field

        // STAGE5 - Skip any columns where include data is 'n'

    for (int currentRecord = 0; currentRecord < totalRecords; currentRecord++)
    {
        fileRef << "INSERT INTO `dbc_" + filename + "` VALUES (";
        for (int currentField = 0; currentField < totalFields; currentField++)
        {
            if (!isWDB && (stringSize > 1) && isStringField[currentField])
            {
                int value = *reinterpret_cast<int *>(recordData[currentRecord][currentField]);
                if (value)
                {
                    string outText = "\"";
                    for (int x = value; x < stringSize; x++)
                    {
                        if (!stringData[x])
                            break;

                        if (stringData[x] == '"')
                            outText.append("\"");

                        if (stringData[x] == '\r')
                        {
                            outText.append("\\r");
                            continue;
                        }

                        if (stringData[x] == '\n')
                        {
                            outText.append("\\n");
                            continue;
                        }

                        if (stringData[x] == '\\')
                        {
                            outText.append("\\\\");
                            continue;
                        }

                        outText.append(ToStr(stringData[x]));
                    }
                    outText.append("\"");
                    fileRef << outText.c_str();
                }
                else
                {
                    fileRef << '"' << '"';
                }
            }
            else if (isWDB && isStringField[currentField])
            {
                string _tempText = reinterpret_cast<char *>(recordData[currentRecord][currentField]);
                int value = _tempText.size();
                if (value)
                {
                    string outText = "\"";
                    for (int x = 0; x < value; x++)
                    {
                        if (!_tempText[x])
                            break;

                        if (_tempText[x] == '"')
                            outText.append("\"");

                        if (_tempText[x] == '\r')
                        {
                            outText.append("\\r");
                            continue;
                        }

                        if (_tempText[x] == '\n')
                        {
                            outText.append("\\n");
                            continue;
                        }

                        outText.append(ToStr(_tempText[x]));
                    }
                    outText.append("\"");
                    fileRef << outText.c_str();
                }
                else
                {
                    fileRef << '"' << '"';
                }
            }
            else if (isFloatField[currentField])
                //fprintf(output, "%f", *reinterpret_cast<float *>(recordData[currentRecord][currentField]));
                fileRef << *reinterpret_cast<float *>(recordData[currentRecord][currentField]);
            else if (isByteField[currentField])
                //fprintf(output, "%d", *reinterpret_cast<char *>(recordData[currentRecord][currentField]));
                fileRef << *reinterpret_cast<char *>(recordData[currentRecord][currentField]);
            else if (isIntField[currentField] || isBoolField[currentField])
                //fprintf(output, "%i", *reinterpret_cast<int *>(recordData[currentRecord][currentField]));
                fileRef << *reinterpret_cast<int *>(recordData[currentRecord][currentField]);

            if (currentField+1 < totalFields)
                fileRef << ",";
        }
        fileRef << ");" << endl;
    }
}

inline const char * const BoolToString(bool b)
{
  return b ? "true" : "false";
}

void ExportFiles(ofstream& fileRef, const char* FileName )
{
    string filename2  = string(FileName)+".sql";

    string tempFileStructure = "";
    int recordSize = 0;

    FileStructure sFileStructure;
    sFileStructure.Structure = tempFileStructure;
    sFileStructure.recordSize = recordSize;

    Reader cReader;

    if (CONF_generate_csv_files || CONF_generate_sql_files || CONF_create_xml_file)
    {
        printf ("Generating: ");

        // Read DBC File data into memory
        if (cReader.LoadBinary((char*)FileName, sFileStructure.Structure, sFileStructure.recordSize))
        {
            // open a file in write mode.
            ofstream outfile;
            outfile.open(filename2);

            if(outfile.is_open())
            {
                //Generate CSV files Here
                if (CONF_generate_csv_files)
                {
                    cReader.ExtractBinaryInfo(FileName);
                    printf("CSV ");
                }

                //Build the SQL File Header
                if (CONF_generate_sql_files)
                {
                    cReader.WriteSqlStructure(outfile,CleanFilename(string(FileName)));

                    //Build the SQL File Body Data
                    cReader.WriteSqlData(outfile,CleanFilename(string(FileName)));
                    printf ("SQL ");
                }

                //Generate XML file contents Here
                if (CONF_create_xml_file)
                {
                    if(fileRef.is_open())
                    {
                        cReader.GenerateXml(fileRef,CleanFilename(string(FileName)));
                        printf("XML ");
                    }
                }
            }
            outfile.close();
            printf ("\n");
        }
    }
}

bool FileExists(const char* FileName)
{
    int fp = _open(FileName, OPEN_FLAGS);
    if (fp != -1)
    {
        _close(fp);
        return true;
    }

    return false;
}

void Usage(char* prg)
{
    printf(
        "Usage:\n"
        "%s -[var] [value]\n"
        "-i set input path\n"
        "-o set output path\n"
        "-e extract only MAP(1)/DBC(2) - standard: both(3)\n"
        "-e extract only MAP(1)/DBC(2) - temporary only: DBC(2)\n"
        "-f height stored as int (less map size but lost some accuracy) 1 by default\n"
        "-b extract data for specific build (at least not greater it from available). Min supported build %u.\n"
        "-s generate SQL file from DBC\n"
        "-c generate CSV file from DBC\n"
        "-r remove DBC file after SQL generation\n"
        "-x generate XML Config file"
        "Example: %s -f 0 -i \"c:\\games\\game\"", prg, MIN_SUPPORTED_BUILD, prg);
    exit(1);
}

void HandleArgs(int argc, char * arg[])
{
    for (int c = 1; c < argc; ++c)
    {
        // i - input path
        // o - output path
        // e - extract only MAP(1)/DBC(2) - standard both(3)
        // f - use float to int conversion
        // h - limit minimum height
        // s - generate SQL file from DBC
        // c - generate CSV file from DBC
        // r - remove DBC file after SQL generation\
        // x - generate XML Config file"

        if (arg[c][0] != '-')
            Usage(arg[0]);

        switch (arg[c][1])
        {
            case 'i':
                if (c + 1 < argc)                           // all ok
                    strcpy(input_path, arg[(c++) + 1]);
                else
                    Usage(arg[0]);
                break;
            case 'o':
                if (c + 1 < argc)                           // all ok
                    strcpy(output_path, arg[(c++) + 1]);
                else
                    Usage(arg[0]);
                break;
            case 'f':
                if (c + 1 < argc)                           // all ok
                    CONF_allow_float_to_int = atoi(arg[(c++) + 1]) != 0;
                else
                    Usage(arg[0]);
                break;
            case 'e':
                if (c + 1 < argc)                           // all ok
                {
                    CONF_extract = atoi(arg[(c++) + 1]);
                    if (!(CONF_extract > 0 && CONF_extract < 4))
                        Usage(arg[0]);
                }
                else
                    Usage(arg[0]);
                break;
            case 'b':
                if(c + 1 < argc)                            // all ok
                {
                    CONF_max_build = atoi(arg[(c++) + 1]);
                    if (CONF_max_build < MIN_SUPPORTED_BUILD)
                        Usage(arg[0]);
                }
                else
                    Usage(arg[0]);
                break;
            case 's':
                    CONF_generate_sql_files=true;
                break;

            case 'c':
                    CONF_generate_csv_files=true;
                break;

            case 'r':
                    CONF_remove_dbc=true;
                break;
            case 'x':
                    CONF_create_xml_file=true;
                break;

            default:
                Usage(arg[0]);
                break;
        }
    }
}

void AppendDBCFileListTo(HANDLE mpqHandle, std::set<std::string>& filelist)
{
    SFILE_FIND_DATA findFileData;

    HANDLE searchHandle = SFileFindFirstFile(mpqHandle, "*.dbc", &findFileData, NULL);
    if (!searchHandle)
        return;

    filelist.insert(findFileData.cFileName);

    while (SFileFindNextFile(searchHandle, &findFileData))
        filelist.insert(findFileData.cFileName);

    SFileFindClose(searchHandle);
}

void AppendDB2FileListTo(HANDLE mpqHandle, std::set<std::string>& filelist)
{
    SFILE_FIND_DATA findFileData;

    HANDLE searchHandle = SFileFindFirstFile(mpqHandle, "*.db2", &findFileData, NULL);
    if (!searchHandle)
        return;

    filelist.insert(findFileData.cFileName);

    while (SFileFindNextFile(searchHandle, &findFileData))
        filelist.insert(findFileData.cFileName);

    SFileFindClose(searchHandle);
}

uint32 ReadBuild(int locale)
{
    // include build info file also
    std::string filename  = std::string("component.wow-")+langs[locale]+".txt";
    //printf("Read %s file... ", filename.c_str());

    HANDLE fileHandle;

    if (!OpenNewestFile(filename.c_str(), &fileHandle))
    {
        printf("\nFatal error: Not found %s file!\n", filename.c_str());
        exit(1);
    }

    unsigned int data_size = SFileGetFileSize(fileHandle, NULL);

    std::string text;
    text.resize(data_size);

    if (!SFileReadFile(fileHandle, &text[0], data_size, NULL, NULL))
    {
        printf("\nFatal error: Can't read %s file!\n", filename.c_str());
        exit(1);
    }

    SFileCloseFile(fileHandle);

    size_t pos = text.find("version=\"");
    size_t pos1 = pos + strlen("version=\"");
    size_t pos2 = text.find("\"",pos1);
    if (pos == text.npos || pos2 == text.npos || pos1 >= pos2)
    {
        printf("\nFatal error: Invalid  %s file format!\n", filename.c_str());
        exit(1);
    }

    std::string build_str = text.substr(pos1,pos2-pos1);

    int build = atoi(build_str.c_str());
    if (build <= 0)
    {
        printf("\nFatal error: Invalid  %s file format!\n", filename.c_str());
        exit(1);
    }

    if (build < MIN_SUPPORTED_BUILD)
    {
        printf("\nFatal error: tool can correctly extract data only for build %u or later (detected: %u)!\n", MIN_SUPPORTED_BUILD, build);
        exit(1);
    }

    return build;
}

uint32 ReadMapDBC(int const locale)
{
    HANDLE localeFile;
    char localMPQ[512];
    sprintf(localMPQ, "%s/Data/%s/locale-%s.MPQ", input_path, langs[locale], langs[locale]);
    if (!SFileOpenArchive(localMPQ, 0, MPQ_OPEN_READ_ONLY, &localeFile))
        exit(1);

    printf("Read Map.dbc file... ");

    HANDLE dbcFile;
    if (!SFileOpenFileEx(localeFile, "DBFilesClient\\Map.dbc", SFILE_OPEN_PATCHED_FILE, &dbcFile))
    {
        printf("\nFatal error: Cannot find Map.dbc in archive!\n");
        exit(1);
    }

    DBCFile dbc(dbcFile);

    if (!dbc.open())
    {
        printf("\nFatal error: Invalid Map.dbc file format!\n");
        exit(1);
    }

    size_t map_count = dbc.getRecordCount();
    map_ids = new map_id[map_count];
    for (uint32 x = 0; x < map_count; ++x)
    {
        map_ids[x].id = dbc.getRecord(x).getUInt(0);
        strcpy(map_ids[x].name, dbc.getRecord(x).getString(1));
    }
    printf("Done! (%u maps loaded)\n", map_count);
    return map_count;
}

void ReadAreaTableDBC(int const locale)
{
    HANDLE localeFile;
    char localMPQ[512];
    sprintf(localMPQ, "%s/Data/%s/locale-%s.MPQ", input_path, langs[locale], langs[locale]);
    if (!SFileOpenArchive(localMPQ, 0, MPQ_OPEN_READ_ONLY, &localeFile))
        exit(1);

    printf("Read AreaTable.dbc file...");

    HANDLE dbcFile;
    if (!SFileOpenFileEx(localeFile, "DBFilesClient\\AreaTable.dbc", SFILE_OPEN_PATCHED_FILE, &dbcFile))
    {
        printf("\nFatal error: Cannot find AreaTable.dbc in archive!\n");
        exit(1);
    }

    DBCFile dbc(dbcFile);

    if (!dbc.open())
    {
        printf("\nFatal error: Invalid AreaTable.dbc file format!\n");
        exit(1);
    }

    size_t area_count = dbc.getRecordCount();
    size_t maxid = dbc.getMaxId();
    areas = new uint16[maxid + 1];
    memset(areas, 0xff, (maxid + 1) * sizeof(uint16));

    for (uint32 x = 0; x < area_count; ++x)
        areas[dbc.getRecord(x).getUInt(0)] = dbc.getRecord(x).getUInt(3);

    maxAreaId = dbc.getMaxId();

    printf("Done! (%u areas loaded)\n", area_count);
}

void ReadLiquidTypeTableDBC(int const locale)
{
    HANDLE localeFile;
//    HANDLE localeFile2;
    char localMPQ[512];
//    char localMPQ2[512];

    sprintf(localMPQ, "%s/Data/misc.MPQ", input_path);//, langs[locale], langs[locale]);
    if (FileExists(localMPQ)==false)
    {   // Use misc.mpq
        sprintf(localMPQ, "%s/Data/%s/locale-%s.MPQ", input_path, langs[locale], langs[locale]);
    }

    if (!SFileOpenArchive(localMPQ, 0, MPQ_OPEN_READ_ONLY, &localeFile))
    {
        exit(1);
    }

    printf("Read LiquidType.dbc file...");

    HANDLE dbcFile;
    if (!SFileOpenFileEx(localeFile, "DBFilesClient\\LiquidType.dbc", SFILE_OPEN_PATCHED_FILE, &dbcFile))
    {
        //if (!SFileOpenFileEx(localeFile2, "DBFilesClient\\LiquidType.dbc", SFILE_OPEN_PATCHED_FILE, &dbcFile))
        //{
            printf("\nFatal error: Cannot find LiquidType.dbc in archive!\n");
            exit(1);
        //}
    }

    DBCFile dbc(dbcFile);
    if (!dbc.open())
    {
        printf("\nFatal error: Invalid LiquidType.dbc file format!\n");
        exit(1);
    }

    size_t LiqType_count = dbc.getRecordCount();
    size_t LiqType_maxid = dbc.getMaxId();
    LiqType = new uint16[LiqType_maxid + 1];
    memset(LiqType, 0xff, (LiqType_maxid + 1) * sizeof(uint16));

    for (uint32 x = 0; x < LiqType_count; ++x)
        LiqType[dbc.getRecord(x).getUInt(0)] = dbc.getRecord(x).getUInt(3);

    printf("Done! (%u LiqTypes loaded)\n", LiqType_count);
}

//
// Adt file convertor function and data
//

// Map file format data
static char const* MAP_MAGIC         = "MAPS";
static char const* MAP_VERSION_MAGIC = "v1.2";
static char const* MAP_AREA_MAGIC    = "AREA";
static char const* MAP_HEIGHT_MAGIC  = "MHGT";
static char const* MAP_LIQUID_MAGIC  = "MLIQ";

struct map_fileheader
{
    uint32 mapMagic;
    uint32 versionMagic;
    uint32 buildMagic;
    uint32 areaMapOffset;
    uint32 areaMapSize;
    uint32 heightMapOffset;
    uint32 heightMapSize;
    uint32 liquidMapOffset;
    uint32 liquidMapSize;
    uint32 holesOffset;
    uint32 holesSize;
};

#define MAP_AREA_NO_AREA      0x0001

struct map_areaHeader
{
    uint32 fourcc;
    uint16 flags;
    uint16 gridArea;
};

#define MAP_HEIGHT_NO_HEIGHT  0x0001
#define MAP_HEIGHT_AS_INT16   0x0002
#define MAP_HEIGHT_AS_INT8    0x0004

struct map_heightHeader
{
    uint32 fourcc;
    uint32 flags;
    float  gridHeight;
    float  gridMaxHeight;
};

#define MAP_LIQUID_TYPE_NO_WATER    0x00
#define MAP_LIQUID_TYPE_WATER       0x01
#define MAP_LIQUID_TYPE_OCEAN       0x02
#define MAP_LIQUID_TYPE_MAGMA       0x04
#define MAP_LIQUID_TYPE_SLIME       0x08

#define MAP_LIQUID_TYPE_DARK_WATER  0x10
#define MAP_LIQUID_TYPE_WMO_WATER   0x20

#define MAP_LIQUID_NO_TYPE    0x0001
#define MAP_LIQUID_NO_HEIGHT  0x0002

struct map_liquidHeader
{
    uint32 fourcc;
    uint16 flags;
    uint16 liquidType;
    uint8  offsetX;
    uint8  offsetY;
    uint8  width;
    uint8  height;
    float  liquidLevel;
};

float selectUInt8StepStore(float maxDiff)
{
    return 255 / maxDiff;
}

float selectUInt16StepStore(float maxDiff)
{
    return 65535 / maxDiff;
}
// Temporary grid data store
uint16 area_flags[ADT_CELLS_PER_GRID][ADT_CELLS_PER_GRID];

float V8[ADT_GRID_SIZE][ADT_GRID_SIZE];
float V9[ADT_GRID_SIZE + 1][ADT_GRID_SIZE + 1];
uint16 uint16_V8[ADT_GRID_SIZE][ADT_GRID_SIZE];
uint16 uint16_V9[ADT_GRID_SIZE + 1][ADT_GRID_SIZE + 1];
uint8  uint8_V8[ADT_GRID_SIZE][ADT_GRID_SIZE];
uint8  uint8_V9[ADT_GRID_SIZE + 1][ADT_GRID_SIZE + 1];

uint8 liquid_type[ADT_CELLS_PER_GRID][ADT_CELLS_PER_GRID];
bool  liquid_show[ADT_GRID_SIZE][ADT_GRID_SIZE];
float liquid_height[ADT_GRID_SIZE + 1][ADT_GRID_SIZE + 1];

bool ConvertADT(char *filename, char *filename2, int cell_y, int cell_x, uint32 build)
{
    ADT_file adt;

    if (!adt.loadFile(filename, false))
        return false;

    memset(liquid_show, 0, sizeof(liquid_show));
    memset(liquid_type, 0, sizeof(liquid_type));

    // Prepare map header
    map_fileheader map;
    map.mapMagic = *(uint32 const*)MAP_MAGIC;
    map.versionMagic = *(uint32 const*)MAP_VERSION_MAGIC;
    map.buildMagic = build;

    // Get area flags data
    for (int i = 0; i < ADT_CELLS_PER_GRID; i++)
    {
        for (int j = 0; j < ADT_CELLS_PER_GRID; j++)
        {
            adt_MCNK * cell = adt.cells[i][j];
            uint32 areaid = cell->areaid;
            if (areaid && areaid <= maxAreaId)
            {
                if (areas[areaid] != 0xffff)
                {
                    area_flags[i][j] = areas[areaid];
                    continue;
                }
                printf("File: %s\nCan't find area flag for areaid %u [%d, %d].\n", filename, areaid, cell->ix, cell->iy);
            }
            area_flags[i][j] = 0xffff;
        }
    }
    //============================================
    // Try pack area data
    //============================================
    bool fullAreaData = false;
    uint32 areaflag = area_flags[0][0];
    for (int y = 0; y < ADT_CELLS_PER_GRID; y++)
    {
        for (int x = 0; x < ADT_CELLS_PER_GRID; x++)
        {
            if (area_flags[y][x] != areaflag)
            {
                fullAreaData = true;
                break;
            }
        }
    }

    map.areaMapOffset = sizeof(map);
    map.areaMapSize   = sizeof(map_areaHeader);

    map_areaHeader areaHeader;
    areaHeader.fourcc = *(uint32 const*)MAP_AREA_MAGIC;
    areaHeader.flags = 0;
    if (fullAreaData)
    {
        areaHeader.gridArea = 0;
        map.areaMapSize += sizeof(area_flags);
    }
    else
    {
        areaHeader.flags |= MAP_AREA_NO_AREA;
        areaHeader.gridArea = (uint16)areaflag;
    }

    //
    // Get Height map from grid
    //
    for (int i = 0; i < ADT_CELLS_PER_GRID; i++)
    {
        for (int j = 0; j < ADT_CELLS_PER_GRID; j++)
        {
            adt_MCNK * cell = adt.cells[i][j];
            if (!cell)
                continue;
            // Height values for triangles stored in order:
            // 1     2     3     4     5     6     7     8     9
            //    10    11    12    13    14    15    16    17
            // 18    19    20    21    22    23    24    25    26
            //    27    28    29    30    31    32    33    34
            // . . . . . . . .
            // For better get height values merge it to V9 and V8 map
            // V9 height map:
            // 1     2     3     4     5     6     7     8     9
            // 18    19    20    21    22    23    24    25    26
            // . . . . . . . .
            // V8 height map:
            //    10    11    12    13    14    15    16    17
            //    27    28    29    30    31    32    33    34
            // . . . . . . . .

            // Set map height as grid height
            for (int y = 0; y <= ADT_CELL_SIZE; y++)
            {
                int cy = i * ADT_CELL_SIZE + y;
                for (int x = 0; x <= ADT_CELL_SIZE; x++)
                {
                    int cx = j * ADT_CELL_SIZE + x;
                    V9[cy][cx] = cell->ypos;
                }
            }
            for (int y = 0; y < ADT_CELL_SIZE; y++)
            {
                int cy = i * ADT_CELL_SIZE + y;
                for (int x = 0; x < ADT_CELL_SIZE; x++)
                {
                    int cx = j * ADT_CELL_SIZE + x;
                    V8[cy][cx] = cell->ypos;
                }
            }
            // Get custom height
            adt_MCVT *v = cell->getMCVT();
            if (!v)
                continue;
            // get V9 height map
            for (int y = 0; y <= ADT_CELL_SIZE; y++)
            {
                int cy = i * ADT_CELL_SIZE + y;
                for (int x = 0; x <= ADT_CELL_SIZE; x++)
                {
                    int cx = j * ADT_CELL_SIZE + x;
                    V9[cy][cx] += v->height_map[y * (ADT_CELL_SIZE * 2 + 1) + x];
                }
            }
            // get V8 height map
            for (int y = 0; y < ADT_CELL_SIZE; y++)
            {
                int cy = i * ADT_CELL_SIZE + y;
                for (int x = 0; x < ADT_CELL_SIZE; x++)
                {
                    int cx = j * ADT_CELL_SIZE + x;
                    V8[cy][cx] += v->height_map[y * (ADT_CELL_SIZE * 2 + 1) + ADT_CELL_SIZE + 1 + x];
                }
            }
        }
    }
    //============================================
    // Try pack height data
    //============================================
    float maxHeight = -20000;
    float minHeight =  20000;
    for (int y = 0; y < ADT_GRID_SIZE; y++)
    {
        for (int x = 0; x < ADT_GRID_SIZE; x++)
        {
            float h = V8[y][x];
            if (maxHeight < h) maxHeight = h;
            if (minHeight > h) minHeight = h;
        }
    }
    for (int y = 0; y <= ADT_GRID_SIZE; y++)
    {
        for (int x = 0; x <= ADT_GRID_SIZE; x++)
        {
            float h = V9[y][x];
            if (maxHeight < h) maxHeight = h;
            if (minHeight > h) minHeight = h;
        }
    }

    // Check for allow limit minimum height (not store height in deep ochean - allow save some memory)
    if (CONF_allow_height_limit && minHeight < CONF_use_minHeight)
    {
        for (int y = 0; y < ADT_GRID_SIZE; y++)
            for (int x = 0; x < ADT_GRID_SIZE; x++)
                if (V8[y][x] < CONF_use_minHeight)
                    V8[y][x] = CONF_use_minHeight;
        for (int y = 0; y <= ADT_GRID_SIZE; y++)
            for (int x = 0; x <= ADT_GRID_SIZE; x++)
                if (V9[y][x] < CONF_use_minHeight)
                    V9[y][x] = CONF_use_minHeight;
        if (minHeight < CONF_use_minHeight)
            minHeight = CONF_use_minHeight;
        if (maxHeight < CONF_use_minHeight)
            maxHeight = CONF_use_minHeight;
    }

    map.heightMapOffset = map.areaMapOffset + map.areaMapSize;
    map.heightMapSize = sizeof(map_heightHeader);

    map_heightHeader heightHeader;
    heightHeader.fourcc = *(uint32 const*)MAP_HEIGHT_MAGIC;
    heightHeader.flags = 0;
    heightHeader.gridHeight    = minHeight;
    heightHeader.gridMaxHeight = maxHeight;

    if (maxHeight == minHeight)
        heightHeader.flags |= MAP_HEIGHT_NO_HEIGHT;

    // Not need store if flat surface
    if (CONF_allow_float_to_int && (maxHeight - minHeight) < CONF_flat_height_delta_limit)
        heightHeader.flags |= MAP_HEIGHT_NO_HEIGHT;

    // Try store as packed in uint16 or uint8 values
    if (!(heightHeader.flags & MAP_HEIGHT_NO_HEIGHT))
    {
        float step;
        // Try Store as uint values
        if (CONF_allow_float_to_int)
        {
            float diff = maxHeight - minHeight;
            if (diff < CONF_float_to_int8_limit)      // As uint8 (max accuracy = CONF_float_to_int8_limit/256)
            {
                heightHeader.flags |= MAP_HEIGHT_AS_INT8;
                step = selectUInt8StepStore(diff);
            }
            else if (diff < CONF_float_to_int16_limit) // As uint16 (max accuracy = CONF_float_to_int16_limit/65536)
            {
                heightHeader.flags |= MAP_HEIGHT_AS_INT16;
                step = selectUInt16StepStore(diff);
            }
        }

        // Pack it to int values if need
        if (heightHeader.flags & MAP_HEIGHT_AS_INT8)
        {
            for (int y = 0; y < ADT_GRID_SIZE; y++)
                for (int x = 0; x < ADT_GRID_SIZE; x++)
                    uint8_V8[y][x] = uint8((V8[y][x] - minHeight) * step + 0.5f);
            for (int y = 0; y <= ADT_GRID_SIZE; y++)
                for (int x = 0; x <= ADT_GRID_SIZE; x++)
                    uint8_V9[y][x] = uint8((V9[y][x] - minHeight) * step + 0.5f);
            map.heightMapSize += sizeof(uint8_V9) + sizeof(uint8_V8);
        }
        else if (heightHeader.flags & MAP_HEIGHT_AS_INT16)
        {
            for (int y = 0; y < ADT_GRID_SIZE; y++)
                for (int x = 0; x < ADT_GRID_SIZE; x++)
                    uint16_V8[y][x] = uint16((V8[y][x] - minHeight) * step + 0.5f);
            for (int y = 0; y <= ADT_GRID_SIZE; y++)
                for (int x = 0; x <= ADT_GRID_SIZE; x++)
                    uint16_V9[y][x] = uint16((V9[y][x] - minHeight) * step + 0.5f);
            map.heightMapSize += sizeof(uint16_V9) + sizeof(uint16_V8);
        }
        else
            map.heightMapSize += sizeof(V9) + sizeof(V8);
    }

    // Get liquid map for grid (in WOTLK used MH2O chunk)
    adt_MH2O * h2o = adt.a_grid->getMH2O();
    if (h2o)
    {
        for (int i = 0; i < ADT_CELLS_PER_GRID; i++)
        {
            for (int j = 0; j < ADT_CELLS_PER_GRID; j++)
            {
                adt_liquid_header *h = h2o->getLiquidData(i, j);
                if (!h)
                    continue;

                int count = 0;
                uint64 show = h2o->getLiquidShowMap(h);
                for (int y = 0; y < h->height; y++)
                {
                    int cy = i * ADT_CELL_SIZE + y + h->yOffset;
                    for (int x = 0; x < h->width; x++)
                    {
                        int cx = j * ADT_CELL_SIZE + x + h->xOffset;
                        if (show & 1)
                        {
                            liquid_show[cy][cx] = true;
                            ++count;
                        }
                        show >>= 1;
                    }
                }

                uint32 type = LiqType[h->liquidType];
                switch (type)
                {
                    case LIQUID_TYPE_WATER: liquid_type[i][j] |= MAP_LIQUID_TYPE_WATER; break;
                    case LIQUID_TYPE_OCEAN: liquid_type[i][j] |= MAP_LIQUID_TYPE_OCEAN; break;
                    case LIQUID_TYPE_MAGMA: liquid_type[i][j] |= MAP_LIQUID_TYPE_MAGMA; break;
                    case LIQUID_TYPE_SLIME: liquid_type[i][j] |= MAP_LIQUID_TYPE_SLIME; break;
                    default:
                        printf("\nCan't find Liquid type %u for map %s\nchunk %d,%d\n", h->liquidType, filename, i, j);
                        break;
                }
                // Dark water detect
                if (type == LIQUID_TYPE_OCEAN)
                {
                    uint8 *lm = h2o->getLiquidLightMap(h);
                    if (!lm)
                        liquid_type[i][j] |= MAP_LIQUID_TYPE_DARK_WATER;
                }

                if (!count && liquid_type[i][j])
                    printf("Wrong liquid detect in MH2O chunk");

                float *height = h2o->getLiquidHeightMap(h);
                int pos = 0;
                for (int y = 0; y <= h->height; y++)
                {
                    int cy = i * ADT_CELL_SIZE + y + h->yOffset;
                    for (int x = 0; x <= h->width; x++)
                    {
                        int cx = j * ADT_CELL_SIZE + x + h->xOffset;
                        if (height)
                            liquid_height[cy][cx] = height[pos];
                        else
                            liquid_height[cy][cx] = h->heightLevel1;
                        pos++;
                    }
                }
            }
        }
    }
    else
    {
        // Get from MCLQ chunk (old)
        for (int i = 0; i < ADT_CELLS_PER_GRID; i++)
        {
            for (int j = 0; j < ADT_CELLS_PER_GRID; j++)
            {
                adt_MCNK *cell = adt.cells[i][j];
                if (!cell)
                    continue;

                adt_MCLQ *liquid = cell->getMCLQ();
                int count = 0;
                if (!liquid || cell->sizeMCLQ <= 8)
                    continue;

                for (int y = 0; y < ADT_CELL_SIZE; y++)
                {
                    int cy = i * ADT_CELL_SIZE + y;
                    for (int x = 0; x < ADT_CELL_SIZE; x++)
                    {
                        int cx = j * ADT_CELL_SIZE + x;
                        if (liquid->flags[y][x] != 0x0F)
                        {
                            liquid_show[cy][cx] = true;
                            if (liquid->flags[y][x] & (1 << 7))
                                liquid_type[i][j] |= MAP_LIQUID_TYPE_DARK_WATER;
                            ++count;
                        }
                    }
                }

                uint32 c_flag = cell->flags;
                if (c_flag & (1 << 2))
                    liquid_type[i][j] |= MAP_LIQUID_TYPE_WATER;          // water
                if (c_flag & (1 << 3))
                    liquid_type[i][j] |= MAP_LIQUID_TYPE_OCEAN;          // ochean
                if (c_flag & (1 << 4))
                    liquid_type[i][j] |= MAP_LIQUID_TYPE_MAGMA;          // magma/slime

                if (!count && liquid_type[i][j])
                    printf("Wrong liquid detect in MCLQ chunk");

                for (int y = 0; y <= ADT_CELL_SIZE; y++)
                {
                    int cy = i * ADT_CELL_SIZE + y;
                    for (int x = 0; x <= ADT_CELL_SIZE; x++)
                    {
                        int cx = j * ADT_CELL_SIZE + x;
                        liquid_height[cy][cx] = liquid->liquid[y][x].height;
                    }
                }
            }
        }
    }

    //============================================
    // Pack liquid data
    //============================================
    uint8 type = liquid_type[0][0];
    bool fullType = false;
    for (int y = 0; y < ADT_CELLS_PER_GRID; y++)
    {
        for (int x = 0; x < ADT_CELLS_PER_GRID; x++)
        {
            if (liquid_type[y][x] != type)
            {
                fullType = true;
                y = ADT_CELLS_PER_GRID;
                break;
            }
        }
    }

    map_liquidHeader liquidHeader;

    // no water data (if all grid have 0 liquid type)
    if (type == 0 && !fullType)
    {
        // No liquid data
        map.liquidMapOffset = 0;
        map.liquidMapSize   = 0;
    }
    else
    {
        int minX = 255, minY = 255;
        int maxX = 0, maxY = 0;
        maxHeight = -20000;
        minHeight = 20000;
        for (int y = 0; y < ADT_GRID_SIZE; y++)
        {
            for (int x = 0; x < ADT_GRID_SIZE; x++)
            {
                if (liquid_show[y][x])
                {
                    if (minX > x) minX = x;
                    if (maxX < x) maxX = x;
                    if (minY > y) minY = y;
                    if (maxY < y) maxY = y;
                    float h = liquid_height[y][x];
                    if (maxHeight < h) maxHeight = h;
                    if (minHeight > h) minHeight = h;
                }
                else
                    liquid_height[y][x] = CONF_use_minHeight;
            }
        }
        map.liquidMapOffset = map.heightMapOffset + map.heightMapSize;
        map.liquidMapSize = sizeof(map_liquidHeader);
        liquidHeader.fourcc = *(uint32 const*)MAP_LIQUID_MAGIC;
        liquidHeader.flags = 0;
        liquidHeader.liquidType = 0;
        liquidHeader.offsetX = minX;
        liquidHeader.offsetY = minY;
        liquidHeader.width   = maxX - minX + 1 + 1;
        liquidHeader.height  = maxY - minY + 1 + 1;
        liquidHeader.liquidLevel = minHeight;

        if (maxHeight == minHeight)
            liquidHeader.flags |= MAP_LIQUID_NO_HEIGHT;

        // Not need store if flat surface
        if (CONF_allow_float_to_int && (maxHeight - minHeight) < CONF_flat_liquid_delta_limit)
            liquidHeader.flags |= MAP_LIQUID_NO_HEIGHT;

        if (!fullType)
            liquidHeader.flags |= MAP_LIQUID_NO_TYPE;

        if (liquidHeader.flags & MAP_LIQUID_NO_TYPE)
            liquidHeader.liquidType = type;
        else
            map.liquidMapSize += sizeof(liquid_type);

        if (!(liquidHeader.flags & MAP_LIQUID_NO_HEIGHT))
            map.liquidMapSize += sizeof(float) * liquidHeader.width * liquidHeader.height;
    }

    // map hole info
    uint16 holes[ADT_CELLS_PER_GRID][ADT_CELLS_PER_GRID];

    if(map.liquidMapOffset)
        map.holesOffset = map.liquidMapOffset + map.liquidMapSize;
    else
        map.holesOffset = map.heightMapOffset + map.heightMapSize;

    map.holesSize = sizeof(holes);
    memset(holes, 0, map.holesSize);

    for(int i = 0; i < ADT_CELLS_PER_GRID; ++i)
    {
        for(int j = 0; j < ADT_CELLS_PER_GRID; ++j)
        {
            adt_MCNK * cell = adt.cells[i][j];
            if(!cell)
                continue;
            holes[i][j] = cell->holes;
        }
    }

    // Ok all data prepared - store it
    FILE *output = fopen(filename2, "wb");
    if (!output)
    {
        printf("Can't create the output file '%s'\n", filename2);
        return false;
    }
    fwrite(&map, sizeof(map), 1, output);
    // Store area data
    fwrite(&areaHeader, sizeof(areaHeader), 1, output);
    if (!(areaHeader.flags & MAP_AREA_NO_AREA))
        fwrite(area_flags, sizeof(area_flags), 1, output);

    // Store height data
    fwrite(&heightHeader, sizeof(heightHeader), 1, output);
    if (!(heightHeader.flags & MAP_HEIGHT_NO_HEIGHT))
    {
        if (heightHeader.flags & MAP_HEIGHT_AS_INT16)
        {
            fwrite(uint16_V9, sizeof(uint16_V9), 1, output);
            fwrite(uint16_V8, sizeof(uint16_V8), 1, output);
        }
        else if (heightHeader.flags & MAP_HEIGHT_AS_INT8)
        {
            fwrite(uint8_V9, sizeof(uint8_V9), 1, output);
            fwrite(uint8_V8, sizeof(uint8_V8), 1, output);
        }
        else
        {
            fwrite(V9, sizeof(V9), 1, output);
            fwrite(V8, sizeof(V8), 1, output);
        }
    }

    // Store liquid data if need
    if (map.liquidMapOffset)
    {
        fwrite(&liquidHeader, sizeof(liquidHeader), 1, output);
        if (!(liquidHeader.flags & MAP_LIQUID_NO_TYPE))
            fwrite(liquid_type, sizeof(liquid_type), 1, output);
        if (!(liquidHeader.flags & MAP_LIQUID_NO_HEIGHT))
        {
            for (int y = 0; y < liquidHeader.height; y++)
                fwrite(&liquid_height[y + liquidHeader.offsetY][liquidHeader.offsetX], sizeof(float), liquidHeader.width, output);
        }
    }

    // store hole data
    fwrite(holes, map.holesSize, 1, output);

    fclose(output);

    return true;
}

void ExtractMapsFromMpq(uint32 build, const int locale)
{
    char mpq_filename[1024];
    char output_filename[1024];
    char mpq_map_name[1024];

    printf("\nExtracting maps for build %u...\n",build);

    uint32 map_count = ReadMapDBC(locale);

    ReadAreaTableDBC(locale);
    ReadLiquidTypeTableDBC(locale);

    std::string path = output_path;
    path += "/maps/";
    CreateDir(path);

    printf("Convert map files\n");
    for (uint32 z = 0; z < map_count; ++z)
    {
        printf("Extract %s (%d/%d)                  \n", map_ids[z].name, z + 1, map_count);
        // Loadup map grid data
        sprintf(mpq_map_name, "World\\Maps\\%s\\%s.wdt", map_ids[z].name, map_ids[z].name);
        WDT_file wdt;
        if (!wdt.loadFile(mpq_map_name, false))
            continue;

        for (uint32 y = 0; y < WDT_MAP_SIZE; ++y)
        {
            for (uint32 x = 0; x < WDT_MAP_SIZE; ++x)
            {
                if (!wdt.main->adt_list[y][x].exist)
                    continue;
                sprintf(mpq_filename, "World\\Maps\\%s\\%s_%u_%u.adt", map_ids[z].name, map_ids[z].name, x, y);
                sprintf(output_filename, "%s/maps/%03u%02u%02u.map", output_path, map_ids[z].id, y, x);
                ConvertADT(mpq_filename, output_filename, y, x, build);
            }
            // draw progress bar
            printf("Processing........................%d%%\r", (100 * (y + 1)) / WDT_MAP_SIZE);
        }
    }
    delete [] areas;
    delete [] map_ids;
}

void ExtractDBCFiles(int locale, bool basicLocale, uint32& ClientVersion)
{
    printf("Extracting dbc files...\n");

    std::set<std::string> dbcfiles;

    // get DBC file list
    ArchiveSetBounds archives = GetArchivesBounds();
    for(ArchiveSet::const_iterator i = archives.first; i != archives.second;++i)
    {
        AppendDBCFileListTo(*i, dbcfiles);
        AppendDB2FileListTo(*i, dbcfiles);
    }

    std::string path = output_path;
    path += "/dbc/";
    CreateDir(path);
    if(!basicLocale)
    {
        path += langs[locale];
        path += "/";
        CreateDir(path);
    }

    // extract Build info file
    {
        std::string mpq_name = std::string("component.wow-") + langs[locale] + ".txt";
        std::string filename = path + mpq_name;

        ExtractFile(mpq_name.c_str(), filename);
    }

    // extract DBCs
    int count = 0;
    int xmlError = 0;
    //Generate XML file Here
    ofstream xmlfile;
    if (CONF_create_xml_file)
    {
        //Check if config file exists
        ifstream my_file("ad_generated.xml");
        if (my_file.good())
        {   //It does, delete it
            my_file.close();
            if( remove( "ad_generated.xml" ) != 0 )
            {
                //Failed to delete the file, prevent any more operations on the file
                perror( "\nError deleting file: ad_generated.xml");
                xmlError=1;
            }
        }

        if(xmlError == 0)
        {
            xmlfile.open( "ad_generated.xml", ios::out | ios::app);
            xmlfile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl;
            xmlfile << "<root>" << endl;
            xmlfile << "    <Config>" << endl;
            xmlfile << "        <Lang_Count>12</Lang_Count>" << endl;
            xmlfile << "        <Languages>\"enGB\", \"enUS\", \"deDE\", \"esES\", \"frFR\", \"koKR\", \"zhCN\", \"zhTW\", \"enCN\", \"enTW\", \"esMX\", \"ruRU\"</Languages>" << endl;
            xmlfile << "        <Min_Supported_Build>" << ClientVersion << "</Min_Supported_Build>" << endl;
            xmlfile << "        <Expansion_Count>4</Expansion_Count>" << endl;
            xmlfile << "        <World_Count>2</World_Count>" << endl;
            xmlfile << "    </Config>" << endl;
            xmlfile << "    <Files>" << endl;
        }
    }

    for (std::set<std::string>::iterator iter = dbcfiles.begin(); iter != dbcfiles.end(); ++iter)
    {
        std::string filename = path;
        filename += (iter->c_str() + strlen("DBFilesClient\\"));

        if (ExtractFile(iter->c_str(), filename))
        {
            printf ("Extracted %s ",filename.c_str());
            //Generate SQL / CSV files Here
            if (CONF_generate_sql_files || CONF_generate_csv_files || CONF_create_xml_file)
            {
                ExportFiles(xmlfile,filename.c_str());
            }
            else
            {
                printf ("\n");
            }
        }

        //Remove DBC file Here
        if (CONF_remove_dbc)
        {
                if( remove( filename.c_str() ) != 0 )
                {
                    //Failed to delete the file, prevent any more operations on the file
                    printf( "\nError deleting file: %s",filename);
                }
        }
            ++count;
    }

    //Generate XML File trailing section Here
    if (xmlError == 0)
    {
        if (CONF_create_xml_file)
        {
            //Close off the XML
            xmlfile << "    </Files>" << endl;
            xmlfile << "</root>" << endl;

            xmlfile.close();
            printf("XML config file created\n");
        }
    }

    printf("Extracted %u DBC/DB2 files\n\n", count);
}

typedef std::pair<std::string /*full_filename*/, char const* /*locale_prefix*/> UpdatesPair;
typedef std::map<int /*build*/, UpdatesPair> Updates;

void AppendPatchMPQFilesToList(char const* subdir, char const* suffix, char const* section, Updates& updates)
{
    char dirname[512];
    if (subdir)
        sprintf(dirname,"%s/Data/%s", input_path, subdir);
    else
        sprintf(dirname,"%s/Data", input_path);

    char scanname[512];
    if (suffix)
        sprintf(scanname,"wow-update-%s-%%u.MPQ", suffix);
    else
        sprintf(scanname,"wow-update-%%u.MPQ");

#ifdef WIN32

    char maskname[512];
    if (suffix)
        sprintf(maskname,"%s/wow-update-%s-*.MPQ", dirname, suffix);
    else
        sprintf(maskname,"%s/wow-update-*.MPQ", dirname);

    WIN32_FIND_DATA ffd;
    HANDLE hFind = FindFirstFile(maskname, &ffd);

    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            int ubuild = 0;
            if (sscanf(ffd.cFileName, scanname, &ubuild) == 1 && (!CONF_max_build || ubuild <= CONF_max_build))
                updates[ubuild] = UpdatesPair(ffd.cFileName, section);
        }
        while (FindNextFile(hFind, &ffd) != 0);

        FindClose(hFind);
    }

#else

    if (DIR *dp  = opendir(dirname))
    {
        int ubuild = 0;
        dirent *dirp;
        while ((dirp = readdir(dp)) != NULL)
            if (sscanf(dirp->d_name, scanname, &ubuild) == 1 && (!CONF_max_build || ubuild <= CONF_max_build))
                updates[ubuild] = UpdatesPair(dirp->d_name, section);

        closedir(dp);
    }

#endif
}

void LoadLocaleMPQFiles(int const locale)
{
    char filename[512];
    HANDLE localeMpqHandle;

    // first base old version of dbc files
    sprintf(filename,"%s/Data/%s/locale-%s.MPQ", input_path, langs[locale], langs[locale]);
    if (FileExists(filename)==true)
    {
        if (!OpenArchive(filename, &localeMpqHandle))
        {
            printf("\nError open archive: %s\n\n", filename);
            return;
        }
    }

    sprintf(filename,"%s/Data/misc.MPQ", input_path);//, langs[locale], langs[locale]);
    if (FileExists(filename)==true)
    {
        if (!OpenArchive(filename, &localeMpqHandle))
        {
            printf("\nError open archive: %s\n\n", filename);
            return;
        }
    }

    // prepare sorted list patches in locale dir and Data root
    Updates updates;
    // now update to newer view, locale
    AppendPatchMPQFilesToList(langs[locale], langs[locale], NULL, updates);
    // now update to newer view, root
    AppendPatchMPQFilesToList(NULL, NULL, langs[locale], updates);

    for (Updates::const_iterator itr = updates.begin(); itr != updates.end(); ++itr)
    {
        if (!itr->second.second)
            sprintf(filename,"%s/Data/%s/%s", input_path, langs[locale], itr->second.first.c_str());
        else
            sprintf(filename,"%s/Data/%s", input_path, itr->second.first.c_str());

        //if (!OpenArchive(filename))
        if (!SFileOpenPatchArchive(localeMpqHandle, filename, itr->second.second ? itr->second.second : "", 0))
            printf("\nError open patch archive: %s\n\n", filename);
    }
}

void LoadBaseMPQFiles()
{
    char filename[512];
    HANDLE worldMpqHandle;

    printf("Loaded MPQ files for map extraction:\n");
    for (int i = 1; i <= WORLD_COUNT; i++)
    {
        sprintf(filename, "%s/Data/World%s.MPQ", input_path, (i == 2 ? "2" : ""));
        printf("%s\n", filename);

        if (!OpenArchive(filename, &worldMpqHandle))
        {
            printf("\nError open archive: %s\n\n", filename);
            return;
        }
    }

    for (int i = 1; i <= EXPANSION_COUNT; i++)
    {
        sprintf(filename, "%s/Data/Expansion%i.MPQ", input_path, i);
        printf("%s\n", filename);

        if (!OpenArchive(filename, &worldMpqHandle))
        {
            printf("\nError open archive: %s\n\n", filename);
            return;
        }
    }

    //Add Misc.MPQ
    sprintf(filename, "%s/Data/misc.MPQ", input_path);//, (i == 2 ? "2" : ""));
    if (FileExists(filename)==true)
    {
        printf("%s\n", filename);
        if (!OpenArchive(filename, &worldMpqHandle))
        {
            printf("\nError open archive: %s\n\n", filename);
            return;
        }
    }

    // prepare sorted list patches in Data root
    Updates updates;
    // now update to newer view, root -base
    AppendPatchMPQFilesToList(NULL, NULL, "base", updates);
    // now update to newer view, root -base
    AppendPatchMPQFilesToList(NULL, "base", NULL, updates);

    for (Updates::const_iterator itr = updates.begin(); itr != updates.end(); ++itr)
    {
        sprintf(filename,"%s/Data/%s", input_path, itr->second.first.c_str());

        printf("%s\n", filename);

        if (!OpenArchive(filename, &worldMpqHandle))
        {
            printf("\nError open patch archive: %s\n\n", filename);
            return;
        }
    }
}

int main(int argc, char * arg[])
{
    printf("\nMap & DBC Extractor v2\n");
    printf("======================\n\n");

    HandleArgs(argc, arg);

    int FirstLocale = -1;
    uint32 build = 0;

    ifstream xmlfile ("ad.xml");
    string thisLine;
    if (xmlfile.is_open())
    {
        printf("..Reading ad.xml config\n");

        while ( xmlfile.good() )
        {
          std::getline(xmlfile,thisLine);
            if (thisLine.find("<Min_Supported_Build>") != string::npos)
            {
            //.. found.
                string result=thisLine;
                replaceAll(result,"Min_Supported_Build","");
                replaceAll(result,"/","");
                replaceAll(result,"<>","");
                replaceAll(result," ","");

                printf("Min_Supported_Build=%s\n" , result);
                int numb;
                istringstream ( result ) >> numb;
                MIN_SUPPORTED_BUILD = numb;
            }
            else if (thisLine.find("<Lang_Count>") != string::npos)
            {
                string result=thisLine;
                replaceAll(result,"Lang_Count","");
                replaceAll(result,"/","");
                replaceAll(result,"<>","");
                replaceAll(result," ","");

                printf("No. Languages=%s\n" , result);
                int numb;
                istringstream ( result ) >> numb;
                LANG_COUNT = numb;
                FIRST_LOCALE = 0;
            }
            else if (thisLine.find("<Expansion_Count>") != string::npos)
            {
                string result=thisLine;
                replaceAll(result,"Expansion_Count","");
                replaceAll(result,"/","");
                replaceAll(result,"<>","");
                replaceAll(result," ","");

                printf("No. Expansions=%s\n" , result);
                int numb;
                istringstream ( result ) >> numb;
                EXPANSION_COUNT = numb;
            }
            else if (thisLine.find("<World_Count>") != string::npos)
            {
                string result=thisLine;
                replaceAll(result,"World_Count","");
                replaceAll(result,"/","");
                replaceAll(result,"<>","");
                replaceAll(result," ","");

                printf("No. worlds=%s\n" , result);
                int numb;
                istringstream ( result ) >> numb;
                WORLD_COUNT = numb;
            }
            //else if (thisLine.find("<ClientLanguages>") != string::npos)
            //{
            //    string result=thisLine;
            //    replaceAll(result,"ClientLanguages","");
            //    replaceAll(result,"/","");
            //    replaceAll(result,"<>","");
            //    replaceAll(result," ","");

            //    printf("ClientLanguages=%s\n" , result);
            //    langs = new char {result};

            //}
        }
    }

    xmlfile.close();

    for (int i = 0; i < LANG_COUNT; i++)
    {
        char tmp1[512];
        sprintf(tmp1, "%s/Data/%s/locale-%s.MPQ", input_path, langs[i], langs[i]);
        if (FileExists(tmp1))
        {
            printf("Detected locale: %s\n", langs[i]);

            //Open MPQs
            LoadLocaleMPQFiles(i);

            if((CONF_extract & EXTRACT_DBC) == 0)
            {
                FIRST_LOCALE = i;
                CURRENT_BUILD = ReadBuild(FIRST_LOCALE);
                printf("Detected client build: %u\n", CURRENT_BUILD);
                break;
            }

            //Extract DBC files
            if(FirstLocale < 0)
            {
                FirstLocale = i;
                CURRENT_BUILD = ReadBuild(FIRST_LOCALE);
                printf("Detected client build: %u\n", CURRENT_BUILD);
                ExtractDBCFiles(i, true,build);
            }
            else
                ExtractDBCFiles(i, false,build);

            //Close MPQs
            CloseArchives();
        }
    }

    if(FIRST_LOCALE < 0)
    {
        printf("No locales detected\n");
        return 0;
    }

    if (CONF_extract & EXTRACT_MAP)
    {
        printf("Using locale: %s\n", langs[FIRST_LOCALE]);

        // Open MPQs
        LoadBaseMPQFiles();
        LoadLocaleMPQFiles(FIRST_LOCALE);

        // Extract maps
        ExtractMapsFromMpq(CURRENT_BUILD, FIRST_LOCALE);

        // Close MPQs
        CloseArchives();
    }

    return 0;
}

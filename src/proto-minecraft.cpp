#include <cstddef>
#include <iostream>
#include <vector>
#include <sstream>

#include <sqlite3.h>
#include <chrono>
#include <string>

#include "massip-addr.c"

extern "C"
{
    void banout_end(struct BannerOutput *banout, unsigned proto);
    void banout_append_unicode(struct BannerOutput *banout, unsigned proto, unsigned c);
    void banout_append(struct BannerOutput *banout, unsigned proto, const void *px, size_t length);
    void tcp_close(struct InteractiveData *more);
}

#include "proto-minecraft.h"
#include "proto-interactive.h"

#define MC_PROTO_MAX_PACKET_SIZE 1024

uint64_t timeSinceEpochMillisec()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static void minecraft_parse([[maybe_unused]] const struct Banner1 *banner1,
                            [[maybe_unused]] void *banner1_private,
                            [[maybe_unused]] struct ProtocolState *pstate,
                            const unsigned char *px, size_t length,
                            struct BannerOutput *banout,
                            struct InteractiveData *more,
                            ipaddress ip_them,
                            unsigned short port_them)
{
    if (length > MC_PROTO_MAX_PACKET_SIZE)
    {
        tcp_close(more);
        return;
    }

    std::string read_data;
    for (size_t i = 0; i < length; i++)
    {
        read_data.append(reinterpret_cast<const char *>(&px[i]), 1);
    }
    std::vector<std::string> process_string;
    std::string temp_data;
    for (size_t t = 8; t < read_data.size(); t += 2)
    {
        char &i = read_data[t];
        char &i2 = read_data[t - 2];
        if ((int)i > 0 && i2 != '\xa7')
        {
            temp_data += read_data[t];
        }
        else if ((int)i == 0 && t != 8)
        {
            process_string.push_back(temp_data);
            temp_data = "";
        }
    }
    if (!temp_data.empty())
    {
        process_string.push_back(temp_data);
    }
    if (process_string.size() >= 4)
    {
        sqlite3 *db;
        int rc;

        /* Open database */
        rc = sqlite3_open("videlicet.db", &db);

        if (rc)
        {
            fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        }

        std::string serverVersion = process_string[1];
        std::string motd = process_string[2];
        int playerCount = -1;
        int maxPlayers = -1;

        try
        {
            playerCount = stoi(process_string[3]);
            maxPlayers = stoi(process_string[4]);
        }
        catch (const std::invalid_argument)
        {
            // FAILED TO CONVERT PLAYER COUNT TO INT
        }

        struct ipaddress_formatted fmt;
        fmt = ipaddress_fmt(ip_them);

        std::string mainString;
        mainString = "INSERT INTO BASIC_PINGS (TIMESTAMP,IP,PORT,VERSION,MOTD,PLAYERS_ONLINE,MAX_PLAYERS) VALUES (?,?,?,?,?,?,?)";

        sqlite3_stmt *statement;
        sqlite3_prepare_v2(db, mainString.c_str(), -1, &statement, NULL);

        sqlite3_bind_int64(statement, 1, timeSinceEpochMillisec());
        sqlite3_bind_text(statement, 2, fmt.string, sizeof(fmt), SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 3, port_them);
        sqlite3_bind_text(statement, 4, serverVersion.c_str(), serverVersion.size(), SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 5, motd.c_str(), motd.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 6, playerCount);
        sqlite3_bind_int(statement, 7, maxPlayers);

        sqlite3_step(statement);
        sqlite3_finalize(statement);
        sqlite3_close(db);
    }
    else
    {
        banout_append(banout, PROTO_MINECRAFT, "PARSING ERROR:", AUTO_LEN);
        banout_append(banout, PROTO_MINECRAFT, px, length);
    }
    banout_end(banout, PROTO_MINECRAFT);
    tcp_close(more);
}

static void *minecraft_init([[maybe_unused]] struct Banner1 *banner1)
{
    return nullptr;
}

static int minecraft_selftest()
{
    return 0;
}

const struct ProtocolParserStream banner_minecraft = {
    "minecraft", 25565, "\xFE\x01", 2, 0,
    minecraft_selftest,
    minecraft_init,
    minecraft_parse,
    nullptr,
    nullptr,
    nullptr};

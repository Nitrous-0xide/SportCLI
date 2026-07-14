#include <ncurses.h>
#include <curl/curl.h>
#include <jansson.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct Memory {
    char *data;
    size_t size;
};

static const char *getCompetitorDisplayName(json_t *competitor) {
    if (!competitor) return "N/A";

    json_t *athlete = json_object_get(competitor, "athlete");
    if (athlete) {
        const char *name = json_string_value(json_object_get(athlete, "displayName"));
        if (name && strlen(name) > 0) return name;
    }

    json_t *team = json_object_get(competitor, "team");
    if (team) {
        const char *name = json_string_value(json_object_get(team, "displayName"));
        if (name && strlen(name) > 0) return name;
    }

    return "N/A";
}

static const char *getCompetitorMetric(json_t *competitor) {
    if (!competitor) return "0";

    const char *score = json_string_value(json_object_get(competitor, "score"));
    if (score && strlen(score) > 0) return score;

    json_t *records = json_object_get(competitor, "records");
    if (records && json_is_array(records) && json_array_size(records) > 0) {
        json_t *record = json_array_get(records, 0);
        if (record) {
            const char *summary = json_string_value(json_object_get(record, "summary"));
            if (summary && strlen(summary) > 0) return summary;
        }
    }

    return "0";
}

static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp);

typedef enum {
    screenMenu,
    screenEventList
} screenState;

typedef struct {
    const char *menuLabel;
    const char *screenTitle;
    const char *scoreboardUrl;
} eventDefinition;

typedef struct {
    char *eventName;
    char *competitionName;
    char *dateText;
    char *venueName;
    char *broadcastName;
    char *homeName;
    char *awayName;
    char *homeScore;
    char *awayScore;
    char *statusDesc;
    char *clock;
} matchItem;

static char *duplicateString(const char *text) {
    if (!text) return NULL;

    size_t textLength = strlen(text);
    char *copy = malloc(textLength + 1);
    if (!copy) return NULL;

    memcpy(copy, text, textLength + 1);
    return copy;
}

static void clearMatchItem(matchItem *matchItem) {
    if (!matchItem) return;

    // Each field is heap-allocated during the fetch pass.
    free(matchItem->eventName);
    free(matchItem->competitionName);
    free(matchItem->dateText);
    free(matchItem->venueName);
    free(matchItem->broadcastName);
    free(matchItem->homeName);
    free(matchItem->awayName);
    free(matchItem->homeScore);
    free(matchItem->awayScore);
    free(matchItem->statusDesc);
    free(matchItem->clock);
    memset(matchItem, 0, sizeof(*matchItem));
}

// the menu and live view both read from this table.
static const eventDefinition eventDefinitions[] = {
    {"FIFA World Cup", "=== ESPN FIFA World Cup Live ===", "https://site.api.espn.com/apis/site/v2/sports/soccer/fifa.world/scoreboard"},
    {"UFC (BETA W.I.P)", "=== ESPN UFC Live ===", "https://site.api.espn.com/apis/site/v2/sports/mma/ufc/scoreboard"},
    {"NBA", "=== ESPN NBA Live ===", "https://site.api.espn.com/apis/site/v2/sports/basketball/nba/scoreboard"}
};

static const size_t eventDefinitionCount = sizeof(eventDefinitions) / sizeof(eventDefinitions[0]);

static const char *getObjectString(json_t *object, const char *key, const char *fallback) {
    if (!object) return fallback;

    const char *value = json_string_value(json_object_get(object, key));
    if (value && strlen(value) > 0) return value;

    return fallback;
}

static const char *getVenueName(json_t *competition) {
    json_t *venue = json_object_get(competition, "venue");
    if (!venue) return "N/A";

    const char *venueName = getObjectString(venue, "displayName", NULL);
    if (venueName) return venueName;

    return getObjectString(venue, "fullName", "N/A");
}

static const char *getBroadcastName(json_t *competition) {
    json_t *broadcasts = json_object_get(competition, "broadcasts");
    if (!broadcasts || !json_is_array(broadcasts) || json_array_size(broadcasts) == 0) return "N/A";

    json_t *broadcast = json_array_get(broadcasts, 0);
    if (!broadcast) return "N/A";

    json_t *names = json_object_get(broadcast, "names");
    if (!names || !json_is_array(names) || json_array_size(names) == 0) return "N/A";

    const char *name = json_string_value(json_array_get(names, 0));
    if (name && strlen(name) > 0) return name;

    return "N/A";
}

static const char *getEventName(json_t *event) {
    const char *name = getObjectString(event, "shortName", NULL);
    if (name) return name;

    return getObjectString(event, "name", "N/A");
}

static const char *getCompetitionName(json_t *competition) {
    const char *name = getObjectString(competition, "shortName", NULL);
    if (name) return name;

    return getObjectString(competition, "name", "N/A");
}

static const char *getEventDate(json_t *event) {
    return getObjectString(event, "date", "N/A");
}

static const char *getStatusDescription(json_t *status) {
    json_t *statusType = json_object_get(status, "type");
    return getObjectString(statusType, "description", "Unknown");
}

static size_t buildMatchList(json_t *events, matchItem *matchItems, size_t maxItems) {
    if (!events || !json_is_array(events) || !matchItems || maxItems == 0) return 0;

    size_t matchCount = 0;
    size_t eventCount = json_array_size(events);

    // Copy the strings we need so the JSON response can be freed after rendering.
    for (size_t i = 0; i < eventCount && matchCount < maxItems; i++) {
        json_t *event = json_array_get(events, i);
        json_t *competitions = json_object_get(event, "competitions");
        if (!competitions || !json_is_array(competitions)) continue;

        json_t *competition = json_array_get(competitions, 0);
        if (!competition) continue;

        json_t *competitors = json_object_get(competition, "competitors");
        if (!competitors || !json_is_array(competitors) || json_array_size(competitors) < 2) continue;

        json_t *home = json_array_get(competitors, 0);
        json_t *away = json_array_get(competitors, 1);
        json_t *status = json_object_get(competition, "status");
        if (!home || !away || !status) continue;

        matchItems[matchCount].eventName = duplicateString(getEventName(event));
        matchItems[matchCount].competitionName = duplicateString(getCompetitionName(competition));
        matchItems[matchCount].dateText = duplicateString(getEventDate(event));
        matchItems[matchCount].venueName = duplicateString(getVenueName(competition));
        matchItems[matchCount].broadcastName = duplicateString(getBroadcastName(competition));
        matchItems[matchCount].homeName = duplicateString(getCompetitorDisplayName(home));
        matchItems[matchCount].awayName = duplicateString(getCompetitorDisplayName(away));
        matchItems[matchCount].homeScore = duplicateString(getCompetitorMetric(home));
        matchItems[matchCount].awayScore = duplicateString(getCompetitorMetric(away));
        matchItems[matchCount].statusDesc = duplicateString(getStatusDescription(status));
        matchItems[matchCount].clock = duplicateString(getObjectString(status, "displayClock", ""));
        matchCount++;
    }

    return matchCount;
}

static void renderMatchLine(const matchItem *matchItem, int row, int selected) {
    if (!matchItem) return;

    if (selected) {
        attron(A_REVERSE);
    } else {
        attron(COLOR_PAIR(2));
    }

    if (strcmp(matchItem->statusDesc, "Scheduled") == 0) {
        mvprintw(row, 0, "%c %-24.24s vs %-24.24s (Not started)", selected ? '>' : ' ', matchItem->homeName, matchItem->awayName);
    } else {
        mvprintw(row, 0, "%c %-24.24s (%s) vs %-24.24s (%s)", selected ? '>' : ' ', matchItem->homeName, matchItem->homeScore, matchItem->awayName, matchItem->awayScore);
    }

    if (selected) {
        attroff(A_REVERSE);
    } else {
        attroff(COLOR_PAIR(2));
    }
}

static void renderMatchDetails(const matchItem *matchItem, int topRow) {
    if (!matchItem) return;

    attron(COLOR_PAIR(1));
    mvprintw(topRow, 0, "=== Match Details ===");
    attroff(COLOR_PAIR(1));

    attron(COLOR_PAIR(2));
    mvprintw(topRow + 1, 0, "Match: %s vs %s", matchItem->homeName, matchItem->awayName);
    mvprintw(topRow + 2, 0, "Event: %s", matchItem->eventName);
    mvprintw(topRow + 3, 0, "Date: %s", matchItem->dateText);
    mvprintw(topRow + 4, 0, "Venue: %s", matchItem->venueName);
    mvprintw(topRow + 5, 0, "Broadcast: %s", matchItem->broadcastName);
    mvprintw(topRow + 6, 0, "Status: %s", matchItem->statusDesc);
    mvprintw(topRow + 7, 0, "Clock: %s", strlen(matchItem->clock) > 0 ? matchItem->clock : "N/A");
    attroff(COLOR_PAIR(2));
}

static void renderMatchScreen(const eventDefinition *eventDefinition, int rows, int cols, int *matchSelection, int *matchOffset) {
    if (!eventDefinition) return;

    (void)cols;

    attron(COLOR_PAIR(1));
    mvprintw(0, 0, "%s", eventDefinition->screenTitle);
    attroff(COLOR_PAIR(1));

    CURL *curl = curl_easy_init();
    struct Memory chunk = {0};
    CURLcode res;
    matchItem matchItems[8];
    size_t matchCount = 0;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, eventDefinition->scoreboardUrl);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15);

        // Fetch once per refresh, then build a local cache for the visible list.
        res = curl_easy_perform(curl);

        if (res == CURLE_OK && chunk.data) {
            json_t *root = json_loads(chunk.data, 0, NULL);
            if (root) {
                json_t *events = json_object_get(root, "events");
                if (events && json_is_array(events)) {
                    matchCount = buildMatchList(events, matchItems, 8);
                }
                json_decref(root);
            }
        }

        curl_easy_cleanup(curl);
        free(chunk.data);
    }

    int visibleCount = rows - 11;
    if (visibleCount < 1) visibleCount = 1;
    if ((size_t)visibleCount > matchCount) visibleCount = (int)matchCount;

    if (matchCount > 0) {
        if (*matchSelection < 0) *matchSelection = 0;
        if (*matchSelection >= (int)matchCount) *matchSelection = (int)matchCount - 1;
        if (*matchOffset < 0) *matchOffset = 0;
        if (*matchOffset > *matchSelection) *matchOffset = *matchSelection;
        if (*matchSelection >= *matchOffset + visibleCount) *matchOffset = *matchSelection - visibleCount + 1;
        if (*matchOffset + visibleCount > (int)matchCount) *matchOffset = (int)matchCount - visibleCount;
        if (*matchOffset < 0) *matchOffset = 0;

        for (int i = 0; i < visibleCount; i++) {
            int matchIndex = *matchOffset + i;
            if (matchIndex >= (int)matchCount) break;
            renderMatchLine(&matchItems[matchIndex], 3 + i, matchIndex == *matchSelection);
        }

        renderMatchDetails(&matchItems[*matchSelection], 4 + visibleCount);
    } else {
        attron(COLOR_PAIR(2));
        mvprintw(3, 0, "No matches available right now.");
        mvprintw(5, 0, "Press [b] to return to the menu.");
        attroff(COLOR_PAIR(2));
    }

    for (size_t i = 0; i < matchCount; i++) {
        clearMatchItem(&matchItems[i]);
    }

    attron(COLOR_PAIR(3));
    mvprintw(rows - 2, 0, "[Up/Down] Select   [b] Return to Main   [r] Refresh Now   [q] Quit");
    mvprintw(rows - 1, 0, "SportCLI | Powered by ESPN data | Made by _nitrous0xide_");
    attroff(COLOR_PAIR(3));
}

static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realSize = size * nmemb;
    struct Memory *mem = (struct Memory*)userp;
    char *ptr = realloc(mem->data, mem->size + realSize + 1);
    if (ptr == NULL) return 0;
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realSize);
    mem->size += realSize;
    mem->data[mem->size] = 0;
    return realSize;
}

int main() {
    setlocale(LC_ALL, "");

    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_CYAN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);

    timeout(10000);

    screenState screen = screenMenu;
    int menuSelection = 0;
    int matchSelection = 0;
    int matchOffset = 0;

    while (1) {
        int rows = 0;
        int cols = 0;

        getmaxyx(stdscr, rows, cols);
        clear();

        if (screen == screenMenu) {
            attron(COLOR_PAIR(1));
            mvprintw(0, 0, "=== Main Menu ===");
            attroff(COLOR_PAIR(1));

            attron(COLOR_PAIR(2));
            for (size_t i = 0; i < eventDefinitionCount; i++) {
                mvprintw(3 + (int)i, 2, "%c %s", menuSelection == (int)i ? '>' : ' ', eventDefinitions[i].menuLabel);
            }
            attroff(COLOR_PAIR(2));

            attron(COLOR_PAIR(3));
            mvprintw(rows - 2, 0, "[Up/Down] Select   [Enter] Open   [q] Quit");
            attroff(COLOR_PAIR(3));

            attron(COLOR_PAIR(2));
            mvprintw(rows - 1, 0, "SportCLI | Powered by ESPN data | Made by _nitrous0xide_");
            attroff(COLOR_PAIR(2));
        } else if (screen == screenEventList) {
            renderMatchScreen(&eventDefinitions[menuSelection], rows, cols, &matchSelection, &matchOffset);
        }

        refresh();

        int ch = getch();
        if (ch == ERR) continue;
        if (ch == 'q' || ch == 'Q') break;
        if (screen == screenMenu) {
            if (ch == KEY_UP || ch == KEY_DOWN) {
                if (ch == KEY_UP) {
                    if (menuSelection > 0) {
                        menuSelection--;
                    }
                } else if (ch == KEY_DOWN) {
                    if (menuSelection + 1 < (int)eventDefinitionCount) {
                        menuSelection++;
                    }
                }
            } else if (ch == '\n' || ch == KEY_ENTER) {
                screen = screenEventList;
                matchSelection = 0;
                matchOffset = 0;
            }
        } else {
            if (ch == KEY_UP) {
                if (matchSelection > 0) {
                    matchSelection--;
                    if (matchSelection < matchOffset) {
                        matchOffset = matchSelection;
                    }
                }
            } else if (ch == KEY_DOWN) {
                matchSelection++;
                if (matchOffset < matchSelection) {
                    matchOffset = matchSelection;
                }
            } else if (ch == 'b' || ch == 'B') {
                screen = screenMenu;
                matchSelection = 0;
                matchOffset = 0;
            } else if (ch == 'r' || ch == 'R') {
                continue;
            }
        }
    }

    endwin();
    return 0;
}

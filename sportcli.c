#include <ncurses.h>
#include <curl/curl.h>
#include <jansson.h>
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

typedef enum {
    SCREEN_MENU,
    SCREEN_FIFA_WORLD_CUP,
    SCREEN_UFC
} Screen;

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
    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_CYAN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);

    timeout(10000);

    Screen screen = SCREEN_MENU;
    int menuSelection = 0;

    while (1) {
        int rows = 0;
        int cols = 0;

        getmaxyx(stdscr, rows, cols);
        clear();

        if (screen == SCREEN_MENU) {
            attron(COLOR_PAIR(1));
            mvprintw(0, 0, "=== Main Menu ===");
            attroff(COLOR_PAIR(1));

            attron(COLOR_PAIR(2));
            mvprintw(3, 2, "%c FIFA World Cup", menuSelection == 0 ? '>' : ' ');
            mvprintw(4, 2, "%c UFC (BETA W.I.P)", menuSelection == 1 ? '>' : ' ');
            attroff(COLOR_PAIR(2));

            attron(COLOR_PAIR(3));
            mvprintw(rows - 2, 0, "[Enter] Open   [q] Quit");
            attroff(COLOR_PAIR(3));

            attron(COLOR_PAIR(2));
            mvprintw(rows - 1, 0, "SportCLI | Powered by ESPN data | Made by _nitrous0xide_");
            attroff(COLOR_PAIR(2));
        } else if (screen == SCREEN_FIFA_WORLD_CUP) {
            attron(COLOR_PAIR(1));
            mvprintw(0, 0, "=== ESPN FIFA World Cup Live ===");
            attroff(COLOR_PAIR(1));

            CURL *curl = curl_easy_init();
            struct Memory chunk = {0};
            CURLcode res;

            if (curl) {
                curl_easy_setopt(curl, CURLOPT_URL, "https://site.api.espn.com/apis/site/v2/sports/soccer/fifa.world/scoreboard");
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15);

                res = curl_easy_perform(curl);

                if (res == CURLE_OK && chunk.data) {
                    json_t *root = json_loads(chunk.data, 0, NULL);
                    if (root) {
                        json_t *events = json_object_get(root, "events");
                        if (events && json_is_array(events)) {
                            size_t n = json_array_size(events);

                            for (size_t i = 0; i < n && i < 8; i++) {
                                json_t *match = json_array_get(events, i);
                                json_t *comps = json_object_get(match, "competitions");
                                if (!comps || !json_is_array(comps)) continue;
                                json_t *comp = json_array_get(comps, 0);
                                if (!comp) continue;
                                json_t *competitors = json_object_get(comp, "competitors");
                                if (!competitors || !json_is_array(competitors) || json_array_size(competitors) < 2) continue;
                                json_t *home = json_array_get(competitors, 0);
                                json_t *away = json_array_get(competitors, 1);
                                json_t *status = json_object_get(comp, "status");
                                if (!home || !away || !status) continue;

                                const char *homeName = getCompetitorDisplayName(home);
                                const char *awayName = getCompetitorDisplayName(away);
                                const char *homeScore = getCompetitorMetric(home);
                                const char *awayScore = getCompetitorMetric(away);
                                const char *clock = json_string_value(json_object_get(status, "displayClock")) ?: "";
                                json_t *statusType = json_object_get(status, "type");
                                const char *statusDesc = (statusType ? json_string_value(json_object_get(statusType, "description")) : NULL) ?: "Unknown";

                                int row = 3 + i * 2;

                                attron(COLOR_PAIR(2));
                                if (strcmp(statusDesc, "Scheduled") == 0) {
                                    mvprintw(row, 0, "%-25s vs %-25s (Not started)", homeName, awayName);
                                } else {
                                    mvprintw(row, 0, "%-25s (%s) vs %-25s (%s)", homeName, homeScore, awayName, awayScore);
                                    if (strlen(clock) > 0) {
                                        mvprintw(row + 1, 0, "   Time: %s | %s", clock, statusDesc);
                                    }
                                }
                                attroff(COLOR_PAIR(2));
                            }
                        }
                        json_decref(root);
                    }
                }
                curl_easy_cleanup(curl);
                free(chunk.data);
            }

            attron(COLOR_PAIR(3));
            mvprintw(rows - 2, 0, "[b] Return to Main   [r] Refresh Now   [q] Quit");
            mvprintw(rows - 1, 0, "SportCLI | Powered by ESPN data | Made by _nitrous0xide_");
            attroff(COLOR_PAIR(3));
        } else if (screen == SCREEN_UFC) {
            attron(COLOR_PAIR(1));
            mvprintw(0, 0, "=== ESPN UFC Live ===");
            attroff(COLOR_PAIR(1));

            CURL *curl = curl_easy_init();
            struct Memory chunk = {0};
            CURLcode res;

            if (curl) {
                curl_easy_setopt(curl, CURLOPT_URL, "https://site.api.espn.com/apis/site/v2/sports/mma/ufc/scoreboard");
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15);

                res = curl_easy_perform(curl);

                if (res == CURLE_OK && chunk.data) {
                    json_t *root = json_loads(chunk.data, 0, NULL);
                    if (root) {
                        json_t *events = json_object_get(root, "events");
                        if (events && json_is_array(events)) {
                            size_t n = json_array_size(events);

                            for (size_t i = 0; i < n && i < 8; i++) {
                                json_t *match = json_array_get(events, i);
                                json_t *comps = json_object_get(match, "competitions");
                                if (!comps || !json_is_array(comps)) continue;
                                json_t *comp = json_array_get(comps, 0);
                                if (!comp) continue;
                                json_t *competitors = json_object_get(comp, "competitors");
                                if (!competitors || !json_is_array(competitors) || json_array_size(competitors) < 2) continue;
                                json_t *home = json_array_get(competitors, 0);
                                json_t *away = json_array_get(competitors, 1);
                                json_t *status = json_object_get(comp, "status");
                                if (!home || !away || !status) continue;

                                json_t *homeTeam = json_object_get(home, "team");
                                json_t *awayTeam = json_object_get(away, "team");
                                const char *homeName = getCompetitorDisplayName(home);
                                const char *awayName = getCompetitorDisplayName(away);
                                const char *homeScore = getCompetitorMetric(home);
                                const char *awayScore = getCompetitorMetric(away);
                                const char *clock = json_string_value(json_object_get(status, "displayClock")) ?: "";
                                json_t *statusType = json_object_get(status, "type");
                                const char *statusDesc = (statusType ? json_string_value(json_object_get(statusType, "description")) : NULL) ?: "Unknown";

                                int row = 3 + i * 2;

                                attron(COLOR_PAIR(2));
                                if (strcmp(statusDesc, "Scheduled") == 0) {
                                    mvprintw(row, 0, "%-25s vs %-25s (Not started)", homeName, awayName);
                                } else {
                                    mvprintw(row, 0, "%-28s %3s - %3s %-28s", homeName, homeScore, awayScore, awayName);
                                    if (strlen(clock) > 0) {
                                        mvprintw(row + 1, 0, "   Time: %s | %s", clock, statusDesc);
                                    }
                                }
                                attroff(COLOR_PAIR(2));
                            }
                        }
                        json_decref(root);
                    }
                }
                curl_easy_cleanup(curl);
                free(chunk.data);
            }

            attron(COLOR_PAIR(3));
            mvprintw(rows - 2, 0, "[b] Return to Main   [r] Refresh Now   [q] Quit");
            mvprintw(rows - 1, 0, "SportCLI | Powered by ESPN data | Made by _nitrous0xide_");
            attroff(COLOR_PAIR(3));
        }

        refresh();

        int ch = getch();
        if (ch == ERR) continue;
        if (ch == 'q' || ch == 'Q') break;
        if (screen == SCREEN_MENU) {
            if (ch == KEY_UP || ch == KEY_DOWN) {
                menuSelection = 1 - menuSelection;
            } else if (ch == '\n' || ch == KEY_ENTER) {
                screen = (menuSelection == 0) ? SCREEN_FIFA_WORLD_CUP : SCREEN_UFC;
            }
        } else if (screen == SCREEN_FIFA_WORLD_CUP) {
            if (ch == 'r' || ch == 'R') continue;
            if (ch == 'b' || ch == 'B') {
                screen = SCREEN_MENU;
            }
        } else if (screen == SCREEN_UFC) {
            if (ch == 'r' || ch == 'R') continue;
            if (ch == 'b' || ch == 'B') {
                screen = SCREEN_MENU;
            }
        }
    }

    endwin();
    return 0;
}

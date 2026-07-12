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

    while (1) {
        clear();

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

                            json_t *homeTeam = json_object_get(home, "team");
                            json_t *awayTeam = json_object_get(away, "team");
                            const char *homeName = (homeTeam ? json_string_value(json_object_get(homeTeam, "displayName")) : NULL) ?: "N/A";
                            const char *awayName = (awayTeam ? json_string_value(json_object_get(awayTeam, "displayName")) : NULL) ?: "N/A";
                            const char *homeScore = json_string_value(json_object_get(home, "score")) ?: "0";
                            const char *awayScore = json_string_value(json_object_get(away, "score")) ?: "0";
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
        mvprintw(20, 0, "[q] Quit   [r] Refresh Now");
        attroff(COLOR_PAIR(3));

        refresh();

        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;
        if (ch == 'r' || ch == 'R') continue;
    }

    endwin();
    return 0;
}

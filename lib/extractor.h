#ifndef WEBSITE_DOWNLOADER_EXTRACTOR_H
#define WEBSITE_DOWNLOADER_EXTRACTOR_H

#include <ctype.h>
#include "includes.h"
#include "slre/slre.h"
#include "database.h"

#define NOT_VALID_URL -2
#define NOT_LOCAL_URL -1

/**
 * Check if the link is local to the given host
 * @param link_url the link url to be checked
 * @return
 */
int is_local_link(const char *link_url) {
    if (strlen(link_url) > 0) {
        char link_head[5];
        if (strlen(link_url) > 4) {
            memcpy(link_head, link_url, 4);
            link_head[4] = '\0';
        }
        if (link_url[0] != '/' &&
            ((strlen(link_url) > 4) &&
             (strcmp(link_head, "http") == 0 ||
              strcmp(link_head, "HTTP") == 0 ||
              strcmp(link_head, "www.") == 0 ||
              strcmp(link_head, "WWW.") == 0)))
            return NOT_LOCAL_URL;
        else
            return EXIT_SUCCESS;
    } else {
        return NOT_VALID_URL;
    }
}

/**
 * Process the header of the response to determine if the page returned is of type html
 * @param header the header of the response
 * @return
 */
int is_html(const char *header) {
    static const char *regex = "text/html";
    struct slre_cap caps[2];
    int j = 0, str_len = strlen(header);

    if (slre_match(regex, header + j, str_len - j, caps, 2, SLRE_IGNORE_CASE) > 0) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}

/**
 * Check if the directory path contains any forbidden characters
 * @param path the directory path to be checked
 * @param size size of the directory path
 * @return
 */
int is_valid_dir_path(const char *path, int size) {
    for (int i = 0; i < size; i++) {
        if (path[i] == '.') {
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

/**
 * Extract the response code from the response header
 * @param header header of the http response
 * @return
 */
int extract_response_code(const char *header) {
    static const char *regex = "HTTP/1[^\\s]+ ([\\d]+)";
    struct slre_cap caps[2];
    int j = 0, str_len = strlen(header);

    if (slre_match(regex, header + j, str_len - j, caps, 2, SLRE_IGNORE_CASE) > 0) {
        if (caps[0].ptr != NULL) {
            char *subpat;
            subpat = (char *) malloc((caps[0].len + 1) * sizeof(char));
            memcpy(subpat, caps[0].ptr, caps[0].len);
            subpat[caps[0].len] = '\0';
            int status_code = atoi(subpat);
            free(subpat);
            return status_code;
        }
    }
    return -1;
}

/**
 * In case of the search engine, extract the search string from the
 * query parameters of the GET request
 * @param header the header of the request
 * @return
 */
char *extract_search_string(const char *header) {
    static const char *regex = "GET /[^=]+=([^\\s&]+)";
    struct slre_cap caps[2];
    char *contains_search_query = strstr(header, "search_query");
    if (contains_search_query == NULL) {
        return NULL;
    }
    int j = 0, str_len = strlen(header);

    if (slre_match(regex, header + j, str_len - j, caps, 2, SLRE_IGNORE_CASE) > 0) {
        if (caps[0].ptr != NULL && strlen(caps[0].ptr) != 0) {
            char *subpat;
            subpat = (char *) malloc((caps[0].len + 1) * sizeof(char));
            memcpy(subpat, caps[0].ptr, caps[0].len);
            subpat[caps[0].len] = '\0';
            return subpat;
        }
    }
    return "";
}

/**
 * Extract the title of the page from the bod of the http response
 * @param content the body of the http response
 * @return
 */
char *extract_title(const char *content) {
    char *pattern_start = strstr(content, "<title>");
    if (pattern_start == NULL) {
        pattern_start = strstr(content, "<TITLE>");
    }

    char *pattern_end = strstr(content, "</title>");
    if (pattern_end == NULL) {
        pattern_end = strstr(content, "</TITLE>");
    }

    if (pattern_start != NULL && pattern_end != NULL) {
        char *subpat = (char *) malloc(BUFSIZ * sizeof(char));
        char *escaped_subpat = (char *) malloc(BUFSIZ * sizeof(char));
        memcpy(subpat, pattern_start + 7, pattern_end - pattern_start - 7);
        subpat[pattern_end - pattern_start - 7] = '\0';
        mysql_escape_string(escaped_subpat, subpat, strlen(subpat));
        free(subpat);
        return escaped_subpat;
    }

    return "";
}

/**
 * Extract the description to be used as tags in the search engine
 * @param connection the MySQL connection parameter
 * @param id the id of the response page for reference to the database
 * @param content the body of the http response
 */
void description_extractor(MYSQL *connection, int id, char *content) {
    char *pattern = strstr(content, "DESCRIPTION\n");
    char *pattern_next;
    char *data = (char *) malloc((BUFSIZ * 1000) * sizeof(char));
    int i = 0, j = 0;
    if (pattern != NULL)
        pattern += 12;
    else
        return;
    while (1) {
        pattern_next = strstr(pattern, "\n");
        if (pattern_next == NULL)
            break;
        if (pattern == pattern_next) {
            pattern++;
        } else {
            if (pattern == strstr(pattern, "       ")) {
                j = pattern_next - (pattern + 7);
                memcpy(data + i, pattern + 7, j);
                i = j + i;
                data[i] = '\0';
                pattern = pattern_next + 1;
            } else {
                break;
            }
        }
    }
    if (strlen(data) > 0) {
        for (i = 0; i < strlen(data); i++) {
            if (data[i] == '\'' || data[i] == '\"')
                data[i] = ' ';
        }
        char escaped_tags[BUFSIZ * 1000];
        int len = mysql_real_escape_string(connection, escaped_tags, data, strlen(data));
        escaped_tags[len] = '\0';
        db_add_tags(connection, id, escaped_tags);
    }
    free(data);
}

/**
 * In case of a 3XX response from the server, look for the Location tag in the
 * header to be used for fetching the page from the new location
 * @param header the header of the http response
 * @param host the hostname being used
 * @return
 */
char *extract_redirect_location(const char *header, const char *host) {
    char regex[BUFSIZ] = {0};
    sprintf(regex, "Location: http://%s([^\r\n]+)", host);
    struct slre_cap caps[2];
    int j = 0, str_len = strlen(header);
    char *subpat = NULL;

    if (slre_match(regex, header + j, str_len - j, caps, 2, SLRE_IGNORE_CASE) > 0) {
        if (caps[0].ptr != NULL) {
            subpat = (char *) malloc((caps[0].len + 1) * sizeof(char));
            memcpy(subpat, caps[0].ptr, caps[0].len);
            subpat[caps[0].len] = '\0';
        }
    }
    return subpat;
}

/**
 * Check if the link is valid. If yes, sanitize the link
 * @param link the link url to be processed
 * @return
 */
int validate_link(char **link) {
    if ((strchr(*link, '(') != NULL) || (strchr(*link, ')') != NULL)) {
        return EXIT_FAILURE;
    }
    char *hash_location;
    if ((hash_location = strchr(*link, '#')) != NULL) {
        *hash_location = '\0';
    }
    if ((hash_location = strchr(*link, '?')) != NULL) {
        *hash_location = '\0';
    }
    return EXIT_SUCCESS;
}

/**
 * urlencode the given link url for safety
 * @param url the url to be encoded
 * @param table the table containing character references
 * @return
 */
char *urlencode(char *url, char *table) {
    char *temp_url = url;
    int i;
    char *enc = (char *) malloc(BUFSIZ * sizeof(char));
    memset(enc, 0, BUFSIZ);
    for (i = 0; *temp_url; temp_url++) {
        if (table[*temp_url]) {
            enc[i] = table[*temp_url];
            i++;
        } else {
            int j = sprintf(enc + i, "%%%02X", *temp_url);
            i += j;
        }
    }

    return enc;
}

/**
 * Shorten the path of the url if relative paths are present
 * @param path the path to be shortened
 */
void path_shortener(char **path) {
    char *dot_position = NULL;
    while ((dot_position = strstr(*path, "/..")) != NULL) {
        char *slash = NULL, *previous_slash;
        int covered = 0;
        while ((slash = strstr(*path + covered, "/")) != NULL && slash != dot_position) {
            previous_slash = slash;
            covered = (int) (slash + 1 - *path);
        }
        dot_position += 3;
        char temp[BUFSIZ];
        strcpy(temp, dot_position);
        strcpy(previous_slash, temp);
    }
}

/**
* Extract the tags to be used as keywords in the search engine
* @param connection the MySQL connection parameter
* @param id the id of the response page for reference to the database
* @param content the body of the http response
*/
void tags_extractor(MYSQL *connection, int id, const char *markup) {
    static const char *regex = "<h1[^>]*>([^<\r\n]*)[^<]*</h1>";
    struct slre_cap caps[4];
    int i, j = 0, str_len = strlen(markup);
    char *tags = (char *) malloc(BUFSIZ * sizeof(char));
    memset(tags, '\0', BUFSIZ);
    while (j < str_len &&
           (i = slre_match(regex, markup + j, str_len - j, caps, 4, SLRE_IGNORE_CASE)) > 0) {
        char *subpat;
        if (caps[0].ptr && caps[0].len) {
            subpat = (char *) malloc((caps[0].len + 1) * sizeof(char));
            memcpy(subpat, caps[0].ptr, caps[0].len);
            subpat[caps[0].len] = '\0';
            if (strlen(tags) == 0)
                sprintf(tags, "%s", subpat);
            else
                sprintf(tags, "%s | %s", tags, subpat);
            free(subpat);
        }
        j += i;
    }
    if (strlen(tags) > 0) {
        char converted_to_ISO_tags[5 * BUFSIZ];
        utf8_to_latin9(converted_to_ISO_tags, tags, strlen(tags));
        char escaped_tags[5 * BUFSIZ];
        int len = mysql_real_escape_string(connection, escaped_tags,
                                           converted_to_ISO_tags, strlen(converted_to_ISO_tags));
        escaped_tags[len] = '\0';
        db_add_tags(connection, id, escaped_tags);
    }
    free(tags);
}

/**
 * Extract the links from the http response body corresponding to the given regular expression
 * After extraction, validate, sanitize the link and insert into the database
 * @param connection the MySQL connection parameter
 * @param id the id of the page being processed
 * @param regex the regular expression specifying the type of the link (e.g. href, src etc.)
 * @param url the url of the page to be used to get the absolute paths of the links
 * @param markup the body of the http response
 */
void regex_link_extractor(MYSQL *connection, int id, const char *regex, const char *url, const char *markup) {
    char table[256] = {0};
    int i = 0;
    for (i = 0; i < 256; i++) {
        table[i] = isalnum(i) || i == '*' || i == '-' || i == '.' ||
                   i == '/' || i == ':' || i == '_' ? i : (i == ' ') ? '+' : 0;
    }
    struct slre_cap caps[2];
    int j = 0, str_len = strlen(markup);
    char current_dir[BUFSIZ];
    sprintf(current_dir, "%.*s", (int) (strrchr(url, '/') - url + 1), url);
    while (j < str_len &&
           (i = slre_match(regex, markup + j, str_len - j, caps, 2, SLRE_IGNORE_CASE)) > 0) {
        char *subpat;
        if (caps[0].ptr && caps[0].len) {
            subpat = (char *) malloc((caps[0].len + 1) * sizeof(char));
            memcpy(subpat, caps[0].ptr, caps[0].len);
            subpat[caps[0].len] = '\0';
            char *encoded_link;
            char *trimmed_link = subpat;
            while (*trimmed_link == ' ' || *trimmed_link == '\r')
                trimmed_link++;
            if (is_local_link(trimmed_link) == 0) {
                char *sanitized_link;
                sanitized_link = (char *) malloc(BUFSIZ * sizeof(char));
                if (trimmed_link[0] != '/') {
                    sprintf(sanitized_link, "%s%s", current_dir, trimmed_link);
                } else {
                    strcpy(sanitized_link, trimmed_link);
                }
                if (!validate_link(&sanitized_link)) {
                    if (strstr(sanitized_link, "..") != NULL) {
                        path_shortener(&sanitized_link);
                    }
                    encoded_link = urlencode(sanitized_link, table);
                    db_insert_unique_link(connection, id, encoded_link);
                    free(encoded_link);
                }
                free(sanitized_link);
            } else {
                encoded_link = urlencode(trimmed_link, table);
                db_insert_external_link(connection, encoded_link);
                free(encoded_link);
            }
            free(subpat);
        }
        j += i;
    }
}

/**
 * parent function to the regex_link_extractor looking for drc and href link urls
 * @param connection the MySQL connection parameter
 * @param id the id of the page being processed
 * @param url the url of the page to be used to get the absolute paths of the links
 * @param markup the body of the http response
 */
void link_extractor(MYSQL *connection, int id, const char *url, const char *markup) {
    static const char *href_regex = "href=\"([^'\"<>]+)\"";
    static const char *src_regex = "src=\"([^'\"<>]+)\"";
    regex_link_extractor(connection, id, href_regex, url, markup);
    regex_link_extractor(connection, id, src_regex, url, markup);
}

#endif //WEBSITE_DOWNLOADER_EXTRACTOR_H

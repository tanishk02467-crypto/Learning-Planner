#ifndef PLANNER_H
#define PLANNER_H

#include <stddef.h>
#include <stdio.h>

#define LP_NAME_LEN 256
#define LP_MAX_IDS 1024
#define LP_MAX_REVIEW_GAPS 32
#define LP_MAX_MISSED_DAYS 256

typedef struct {
    int id;
    char name[LP_NAME_LEN];
    int units;
    int difficulty;
    double value;
    double mastery;
} Topic;

typedef struct { int prereq, topic; } Prerequisite;
typedef struct { int a, b, effort; } Related;

typedef struct {
    int id;
    char name[LP_NAME_LEN];
    long long start, end;
    double weight;
    int optional;
    int topic_ids[LP_MAX_IDS];
    int topic_count;
} Assessment;

typedef struct {
    int id;
    char name[LP_NAME_LEN];
    int skills[LP_MAX_IDS];
    int skill_count;
} Mentor;

typedef struct {
    char kind[32];
    int days;
    int units_per_day;
    int hard_units_per_day;
    int hard_threshold;
    double buffer;
    double review_reserve;
    double mastery_threshold;
    int review_gaps[LP_MAX_REVIEW_GAPS];
    int review_gap_count;
    int missed_days[LP_MAX_MISSED_DAYS];
    int missed_day_count;
} Config;

typedef struct {
    Topic *topics;
    size_t topic_count, topic_capacity;
    Prerequisite *prereqs;
    size_t prereq_count, prereq_capacity;
    Related *related;
    size_t related_count, related_capacity;
    Assessment *assessments;
    size_t assessment_count, assessment_capacity;
    Mentor *mentors;
    size_t mentor_count, mentor_capacity;
    Config cfg;
} Dataset;

typedef enum { STRATEGY_BASELINE, STRATEGY_GREEDY } Strategy;

typedef struct {
    int day;
    int topic;
    int units;
    int review;
} Session;

typedef struct {
    Strategy strategy;
    int *selected;
    int *completion_day;
    Session *sessions;
    size_t session_count, session_capacity;
    int planned_topics;
    int study_units;
    int review_units;
    int total_units;
    int missed_days;
} Plan;

typedef struct {
    double *mastery;
    double *stability;
    double weighted_score;
    double *assessment_scores;
    size_t assessment_count;
    int study_units;
    int review_units;
    int skipped_units;
    char *day_log;
} Simulation;

void dataset_init(Dataset *ds);
void dataset_free(Dataset *ds);
int dataset_load(const char *directory, Dataset *ds, char *error, size_t error_size);
int dataset_validate(const Dataset *ds, char *error, size_t error_size);
void dataset_summary(const Dataset *ds, FILE *out);

const char *strategy_name(Strategy strategy);
int strategy_parse(const char *text, Strategy *strategy);
int plan_build(const Dataset *ds, Strategy strategy, int use_eisenhower, Plan *plan, char *error, size_t error_size);
void plan_free(Plan *plan);
void plan_print(const Dataset *ds, const Plan *plan, FILE *out, int max_sessions);
int plan_export_csv(const Dataset *ds, const Plan *plan, const char *path, char *error, size_t error_size);
int plan_export_json(const Dataset *ds, const Plan *plan, const Simulation *sim, const char *path, char *error, size_t error_size);
void matrix_print(const Dataset *ds, FILE *out, int show_all);
int simulation_run(const Dataset *ds, const Plan *plan, const int *extra_missed, int extra_missed_count, Simulation *sim);
void simulation_free(Simulation *sim);
void simulation_print(const Dataset *ds, const Simulation *sim, FILE *out);
void help_print(FILE *out, const char *program_name);

#endif





#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LP_LINE_LEN 16384
#define LP_FIELD_COUNT 16
#define LP_FIELD_LEN 4096

static void set_error(char *dst, size_t n, const char *message) {
    if (dst && n) snprintf(dst, n, "%s", message ? message : "unknown error");
}

static char *trim(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) ++s;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return s;
}

static int copy_text(char *destination, size_t destination_size, const char *source) {
    size_t length = strlen(source);
    if (length >= destination_size) return 0;
    memcpy(destination, source, length + 1);
    return 1;
}

static int parse_int(const char *s, int *out) {
    char *end = NULL;
    long value;
    errno = 0;
    value = strtol(s, &end, 10);
    if (errno || end == s || *trim(end) != '\0' || value < -2147483647L - 1L || value > 2147483647L) return 0;
    *out = (int)value;
    return 1;
}

static int parse_ll(const char *s, long long *out) {
    char *end = NULL;
    long long value;
    errno = 0;
    value = strtoll(s, &end, 10);
    if (errno || end == s || *trim(end) != '\0') return 0;
    *out = value;
    return 1;
}

static int parse_double_value(const char *s, double *out) {
    char *end = NULL;
    double value;
    errno = 0;
    value = strtod(s, &end);
    if (errno || end == s || *trim(end) != '\0' || !isfinite(value)) return 0;
    *out = value;
    return 1;
}

static int split_csv(const char *line, char fields[LP_FIELD_COUNT][LP_FIELD_LEN]) {
    int count = 0;
    size_t pos = 0;
    int quoted = 0;
    if (!line) return 0;
    memset(fields, 0, sizeof(char) * LP_FIELD_COUNT * LP_FIELD_LEN);
    if (count < LP_FIELD_COUNT) ++count;
    while (*line && count > 0) {
        char c = *line++;
        if (c == '"') {
            if (quoted && *line == '"') {
                if (pos + 1 < LP_FIELD_LEN) fields[count - 1][pos++] = '"';
                ++line;
            } else {
                quoted = !quoted;
            }
        } else if (c == ',' && !quoted) {
            fields[count - 1][pos] = '\0';
            if (count >= LP_FIELD_COUNT) return -1;
            ++count;
            pos = 0;
        } else if (c == '\r' || c == '\n') {
            if (!quoted) break;
            if (pos + 1 < LP_FIELD_LEN) fields[count - 1][pos++] = c;
        } else {
            if (pos + 1 < LP_FIELD_LEN) fields[count - 1][pos++] = c;
        }
    }
    fields[count - 1][pos] = '\0';
    if (quoted) return -1;
    for (int i = 0; i < count; ++i) {
        char *v = trim(fields[i]);
        if (v != fields[i]) memmove(fields[i], v, strlen(v) + 1);
    }
    return count;
}

static int parse_int_list(const char *text, int *values, int max_values) {
    const char *cursor = text;
    int count = 0;
    if (!text || !*text) return 0;
    while (*cursor) {
        const char *separator = strchr(cursor, ';');
        size_t length = separator ? (size_t)(separator - cursor) : strlen(cursor);
        char token[LP_FIELD_LEN];
        int value;
        if (length == 0 || length >= sizeof(token) || count >= max_values) return -1;
        memcpy(token, cursor, length);
        token[length] = '\0';
        if (!parse_int(trim(token), &value)) return -1;
        values[count++] = value;
        if (!separator) break;
        cursor = separator + 1;
        if (!*cursor) return -1;
    }
    return count;
}

static int reserve_array(void **ptr, size_t *capacity, size_t needed, size_t item_size) {
    size_t next;
    void *memory;
    if (needed <= *capacity) return 1;
    next = *capacity ? *capacity : 16;
    while (next < needed) {
        if (next > ((size_t)-1) / 2) return 0;
        next *= 2;
    }
    if (next > ((size_t)-1) / item_size) return 0;
    memory = realloc(*ptr, next * item_size);
    if (!memory) return 0;
    *ptr = memory;
    *capacity = next;
    return 1;
}

static int path_join(char *out, size_t out_size, const char *directory, const char *file) {
    int n = snprintf(out, out_size, "%s/%s", directory, file);
    return n >= 0 && (size_t)n < out_size;
}

static int open_csv(const char *directory, const char *filename, FILE **file, char *error, size_t error_size, int required) {
    char path[1024];
    if (!path_join(path, sizeof(path), directory, filename)) {
        set_error(error, error_size, "dataset path is too long");
        return 0;
    }
    *file = fopen(path, "r");
    if (!*file && required) {
        snprintf(error, error_size, "cannot open %s", path);
        return 0;
    }
    return 1;
}

void dataset_init(Dataset *ds) {
    memset(ds, 0, sizeof(*ds));
    snprintf(ds->cfg.kind, sizeof(ds->cfg.kind), "study");
    ds->cfg.days = 14;
    ds->cfg.units_per_day = 8;
    ds->cfg.hard_units_per_day = 4;
    ds->cfg.hard_threshold = 7;
    ds->cfg.buffer = 1.25;
    ds->cfg.review_reserve = 0.20;
    ds->cfg.mastery_threshold = 0.70;
    ds->cfg.review_gaps[0] = 1;
    ds->cfg.review_gaps[1] = 3;
    ds->cfg.review_gaps[2] = 7;
    ds->cfg.review_gaps[3] = 14;
    ds->cfg.review_gap_count = 4;
}

void dataset_free(Dataset *ds) {
    if (!ds) return;
    free(ds->topics);
    free(ds->prereqs);
    free(ds->related);
    free(ds->assessments);
    free(ds->mentors);
    memset(ds, 0, sizeof(*ds));
}

static int load_topics(const char *dir, Dataset *ds, char *error, size_t error_size) {
    FILE *f = NULL;
    char line[LP_LINE_LEN], fields[LP_FIELD_COUNT][LP_FIELD_LEN];
    int row = 1;
    if (!open_csv(dir, "topics.csv", &f, error, error_size, 1)) return 0;
    if (!fgets(line, sizeof(line), f)) { fclose(f); set_error(error, error_size, "topics.csv is empty"); return 0; }
    while (fgets(line, sizeof(line), f)) {
        Topic t;
        int count = split_csv(line, fields);
        ++row;
        if (count != 6) { snprintf(error, error_size, "topics.csv:%d: expected 6 columns", row); fclose(f); return 0; }
        memset(&t, 0, sizeof(t));
        if (!parse_int(fields[0], &t.id) || !parse_int(fields[2], &t.units) || !parse_int(fields[3], &t.difficulty) ||
            !parse_double_value(fields[4], &t.value) || !parse_double_value(fields[5], &t.mastery)) {
            snprintf(error, error_size, "topics.csv:%d: invalid number", row); fclose(f); return 0;
        }
        if (!copy_text(t.name, sizeof(t.name), fields[1])) { snprintf(error, error_size, "topics.csv:%d: name is too long (max %d characters)", row, LP_NAME_LEN - 1); fclose(f); return 0; }
        if (!reserve_array((void **)&ds->topics, &ds->topic_capacity, ds->topic_count + 1, sizeof(Topic))) {
            fclose(f); set_error(error, error_size, "out of memory while reading topics.csv"); return 0;
        }
        ds->topics[ds->topic_count++] = t;
    }
    fclose(f);
    return 1;
}

static int load_prereqs(const char *dir, Dataset *ds, char *error, size_t error_size) {
    FILE *f = NULL;
    char line[LP_LINE_LEN], fields[LP_FIELD_COUNT][LP_FIELD_LEN];
    int row = 1;
    if (!open_csv(dir, "prereqs.csv", &f, error, error_size, 1)) return 0;
    if (!fgets(line, sizeof(line), f)) { fclose(f); set_error(error, error_size, "prereqs.csv is empty"); return 0; }
    while (fgets(line, sizeof(line), f)) {
        Prerequisite p;
        int count = split_csv(line, fields);
        ++row;
        if (count != 2 || !parse_int(fields[0], &p.prereq) || !parse_int(fields[1], &p.topic)) {
            snprintf(error, error_size, "prereqs.csv:%d: malformed prerequisite row", row); fclose(f); return 0;
        }
        if (!reserve_array((void **)&ds->prereqs, &ds->prereq_capacity, ds->prereq_count + 1, sizeof(Prerequisite))) {
            fclose(f); set_error(error, error_size, "out of memory while reading prereqs.csv"); return 0;
        }
        ds->prereqs[ds->prereq_count++] = p;
    }
    fclose(f);
    return 1;
}

static int load_related(const char *dir, Dataset *ds, char *error, size_t error_size) {
    FILE *f = NULL;
    char line[LP_LINE_LEN], fields[LP_FIELD_COUNT][LP_FIELD_LEN];
    int row = 1;
    if (!open_csv(dir, "related.csv", &f, error, error_size, 0)) return 0;
    if (!f) return 1;
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 1; }
    while (fgets(line, sizeof(line), f)) {
        Related r;
        int count = split_csv(line, fields);
        ++row;
        if (count != 3 || !parse_int(fields[0], &r.a) || !parse_int(fields[1], &r.b) || !parse_int(fields[2], &r.effort)) {
            snprintf(error, error_size, "related.csv:%d: malformed related-topic row", row); fclose(f); return 0;
        }
        if (!reserve_array((void **)&ds->related, &ds->related_capacity, ds->related_count + 1, sizeof(Related))) {
            fclose(f); set_error(error, error_size, "out of memory while reading related.csv"); return 0;
        }
        ds->related[ds->related_count++] = r;
    }
    fclose(f);
    return 1;
}

static int load_assessments(const char *dir, Dataset *ds, char *error, size_t error_size) {
    FILE *f = NULL;
    char line[LP_LINE_LEN], fields[LP_FIELD_COUNT][LP_FIELD_LEN];
    int row = 1;
    if (!open_csv(dir, "assessments.csv", &f, error, error_size, 1)) return 0;
    if (!fgets(line, sizeof(line), f)) { fclose(f); set_error(error, error_size, "assessments.csv is empty"); return 0; }
    while (fgets(line, sizeof(line), f)) {
        Assessment a;
        int count = split_csv(line, fields);
        ++row;
        if (count != 7) { snprintf(error, error_size, "assessments.csv:%d: expected 7 columns", row); fclose(f); return 0; }
        memset(&a, 0, sizeof(a));
        if (!parse_int(fields[0], &a.id) || !parse_ll(fields[2], &a.start) || !parse_ll(fields[3], &a.end) ||
            !parse_double_value(fields[4], &a.weight) || !parse_int(fields[5], &a.optional)) {
            snprintf(error, error_size, "assessments.csv:%d: invalid number", row); fclose(f); return 0;
        }
        if (!copy_text(a.name, sizeof(a.name), fields[1])) { snprintf(error, error_size, "assessments.csv:%d: name is too long (max %d characters)", row, LP_NAME_LEN - 1); fclose(f); return 0; }
        a.topic_count = parse_int_list(fields[6], a.topic_ids, LP_MAX_IDS);
        if (a.topic_count < 0) { snprintf(error, error_size, "assessments.csv:%d: invalid topic list", row); fclose(f); return 0; }
        if (!reserve_array((void **)&ds->assessments, &ds->assessment_capacity, ds->assessment_count + 1, sizeof(Assessment))) {
            fclose(f); set_error(error, error_size, "out of memory while reading assessments.csv"); return 0;
        }
        ds->assessments[ds->assessment_count++] = a;
    }
    fclose(f);
    return 1;
}

static int load_mentors(const char *dir, Dataset *ds, char *error, size_t error_size) {
    FILE *f = NULL;
    char line[LP_LINE_LEN], fields[LP_FIELD_COUNT][LP_FIELD_LEN];
    int row = 1;
    if (!open_csv(dir, "mentors.csv", &f, error, error_size, 0)) return 0;
    if (!f) return 1;
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 1; }
    while (fgets(line, sizeof(line), f)) {
        Mentor m;
        int count = split_csv(line, fields);
        ++row;
        if (count != 3 || !parse_int(fields[0], &m.id)) {
            snprintf(error, error_size, "mentors.csv:%d: malformed mentor row", row); fclose(f); return 0;
        }
        memset(&m, 0, sizeof(m));
        if (!parse_int(fields[0], &m.id)) { snprintf(error, error_size, "mentors.csv:%d: invalid ID", row); fclose(f); return 0; }
        if (!copy_text(m.name, sizeof(m.name), fields[1])) { snprintf(error, error_size, "mentors.csv:%d: name is too long (max %d characters)", row, LP_NAME_LEN - 1); fclose(f); return 0; }
        m.skill_count = parse_int_list(fields[2], m.skills, LP_MAX_IDS);
        if (m.skill_count < 0) { snprintf(error, error_size, "mentors.csv:%d: invalid skill list", row); fclose(f); return 0; }
        if (!reserve_array((void **)&ds->mentors, &ds->mentor_capacity, ds->mentor_count + 1, sizeof(Mentor))) {
            fclose(f); set_error(error, error_size, "out of memory while reading mentors.csv"); return 0;
        }
        ds->mentors[ds->mentor_count++] = m;
    }
    fclose(f);
    return 1;
}

static int load_config(const char *dir, Dataset *ds, char *error, size_t error_size) {
    FILE *f = NULL;
    char line[LP_LINE_LEN];
    if (!open_csv(dir, "config.cfg", &f, error, error_size, 0)) return 0;
    if (!f) return 1;
    while (fgets(line, sizeof(line), f)) {
        char *eq, *key, *value;
        int iv, count;
        double dv;
        key = trim(line);
        if (!*key || *key == '#') continue;
        eq = strchr(key, '=');
        if (!eq) { fclose(f); set_error(error, error_size, "config.cfg: expected key=value"); return 0; }
        *eq = '\0';
        value = trim(eq + 1);
        key = trim(key);
        if (strcmp(key, "kind") == 0) snprintf(ds->cfg.kind, sizeof(ds->cfg.kind), "%s", value);
        else if (strcmp(key, "days") == 0 && parse_int(value, &iv)) ds->cfg.days = iv;
        else if (strcmp(key, "units_per_day") == 0 && parse_int(value, &iv)) ds->cfg.units_per_day = iv;
        else if (strcmp(key, "hard_units_per_day") == 0 && parse_int(value, &iv)) ds->cfg.hard_units_per_day = iv;
        else if (strcmp(key, "hard_threshold") == 0 && parse_int(value, &iv)) ds->cfg.hard_threshold = iv;
        else if (strcmp(key, "buffer") == 0 && parse_double_value(value, &dv)) ds->cfg.buffer = dv;
        else if (strcmp(key, "review_reserve") == 0 && parse_double_value(value, &dv)) ds->cfg.review_reserve = dv;
        else if (strcmp(key, "mastery_threshold") == 0 && parse_double_value(value, &dv)) ds->cfg.mastery_threshold = dv;
        else if (strcmp(key, "review_gaps") == 0) {
            count = parse_int_list(value, ds->cfg.review_gaps, LP_MAX_REVIEW_GAPS);
            if (count < 0) { fclose(f); set_error(error, error_size, "config.cfg: invalid review_gaps list"); return 0; }
            ds->cfg.review_gap_count = count;
        } else if (strcmp(key, "missed_days") == 0) {
            count = parse_int_list(value, ds->cfg.missed_days, LP_MAX_MISSED_DAYS);
            if (count < 0) { fclose(f); set_error(error, error_size, "config.cfg: invalid missed_days list"); return 0; }
            ds->cfg.missed_day_count = count;
        } else {
            snprintf(error, error_size, "config.cfg: unknown or malformed key '%s'", key); fclose(f); return 0;
        }
    }
    fclose(f);
    return 1;
}




static int load_builtin_dataset(Dataset *ds, char *error, size_t error_size) {
    static const Topic topics[] = {
        {0, "Foundations", 4, 2, 50, 0.00},
        {1, "Topic1", 4, 3, 17, 0.78},
        {2, "Topic2", 2, 2, 59, 0.84},
        {3, "Topic3", 5, 4, 76, 0.74},
        {4, "Topic4", 8, 8, 47, 0.02},
        {5, "Topic5", 7, 4, 39, 0.17},
        {6, "Topic6", 6, 3, 13, 0.20},
        {7, "Topic7", 9, 3, 42, 0.02},
        {8, "Topic8", 7, 2, 66, 0.07},
        {9, "Topic9", 5, 9, 41, 0.05},
        {10, "Topic10", 3, 2, 63, 0.24},
        {11, "Topic11", 8, 6, 86, 0.27}
    };
    static const Prerequisite prereqs[] = {
        {0,1}, {0,2}, {0,3}, {1,4}, {0,4}, {0,5}, {4,6}, {5,6},
        {3,7}, {6,7}, {4,8}, {7,9}, {9,10}, {0,11}, {7,11}
    };
    static const Related related[] = {
        {1,0,6}, {2,0,6}, {3,2,6}, {4,0,3}, {5,3,8}, {6,5,5},
        {7,2,4}, {8,3,12}, {9,4,8}, {10,0,3}, {11,1,5}, {1,6,7},
        {5,3,11}, {3,0,9}, {9,7,4}, {4,3,9}, {6,7,10}, {2,7,2},
        {9,11,7}, {7,2,2}, {9,0,1}, {5,6,12}
    };
    static const Assessment assessments[] = {
        {0, "Exam0", 16470, 16530, 0.21, 0, {1,3,7,9}, 4},
        {1, "Exam1", 15030, 15120, 0.26, 0, {2,9,11}, 3}
    };
    static const Mentor mentors[] = {
        {0, "Mentor0", {4,8}, 2},
        {1, "Mentor1", {4,6,8,10}, 4},
        {2, "Mentor2", {1,5,8,11}, 4}
    };

    dataset_init(ds);
    ds->topics = (Topic *)malloc(sizeof(topics));
    ds->prereqs = (Prerequisite *)malloc(sizeof(prereqs));
    ds->related = (Related *)malloc(sizeof(related));
    ds->assessments = (Assessment *)malloc(sizeof(assessments));
    ds->mentors = (Mentor *)malloc(sizeof(mentors));
    if (!ds->topics || !ds->prereqs || !ds->related || !ds->assessments || !ds->mentors) {
        dataset_free(ds);
        if (error && error_size) snprintf(error, error_size, "%s", "out of memory loading built-in sample dataset");
        return 0;
    }
    memcpy(ds->topics, topics, sizeof(topics));
    memcpy(ds->prereqs, prereqs, sizeof(prereqs));
    memcpy(ds->related, related, sizeof(related));
    memcpy(ds->assessments, assessments, sizeof(assessments));
    memcpy(ds->mentors, mentors, sizeof(mentors));
    ds->topic_count = ds->topic_capacity = sizeof(topics) / sizeof(topics[0]);
    ds->prereq_count = ds->prereq_capacity = sizeof(prereqs) / sizeof(prereqs[0]);
    ds->related_count = ds->related_capacity = sizeof(related) / sizeof(related[0]);
    ds->assessment_count = ds->assessment_capacity = sizeof(assessments) / sizeof(assessments[0]);
    ds->mentor_count = ds->mentor_capacity = sizeof(mentors) / sizeof(mentors[0]);
    snprintf(ds->cfg.kind, sizeof(ds->cfg.kind), "%s", "study");
    ds->cfg.days = 14;
    ds->cfg.units_per_day = 8;
    ds->cfg.hard_units_per_day = 4;
    ds->cfg.hard_threshold = 7;
    ds->cfg.buffer = 1.25;
    ds->cfg.review_reserve = 0.20;
    ds->cfg.mastery_threshold = 0.70;
    ds->cfg.review_gaps[0] = 1;
    ds->cfg.review_gaps[1] = 3;
    ds->cfg.review_gaps[2] = 7;
    ds->cfg.review_gaps[3] = 14;
    ds->cfg.review_gap_count = 4;
    return 1;
}

int dataset_load(const char *directory, Dataset *ds, char *error, size_t error_size) {
    if (directory && (strcmp(directory, "builtin") == 0 || strcmp(directory, "sample") == 0)) {
        return load_builtin_dataset(ds, error, error_size);
    }
    dataset_init(ds);
    if (!load_topics(directory, ds, error, error_size) ||
        !load_prereqs(directory, ds, error, error_size) ||
        !load_related(directory, ds, error, error_size) ||
        !load_assessments(directory, ds, error, error_size) ||
        !load_mentors(directory, ds, error, error_size) ||
        !load_config(directory, ds, error, error_size)) {
        dataset_free(ds);
        return 0;
    }
    return 1;
}

int dataset_validate(const Dataset *ds, char *error, size_t error_size) {
    size_t n = ds->topic_count;
    int *indegree = NULL, *queue = NULL;
    size_t i, head = 0, tail = 0, processed = 0;
    if (n == 0) { set_error(error, error_size, "dataset contains no topics"); return 0; }
    if (ds->cfg.days < 1 || ds->cfg.days > 100000) { set_error(error, error_size, "days must be between 1 and 100000"); return 0; }
    if (ds->cfg.units_per_day < 1 || ds->cfg.units_per_day > 48) { set_error(error, error_size, "units_per_day must be between 1 and 48"); return 0; }
    if (ds->cfg.hard_units_per_day < 0 || ds->cfg.hard_units_per_day > ds->cfg.units_per_day) { set_error(error, error_size, "hard_units_per_day is outside the daily capacity"); return 0; }
    if (ds->cfg.hard_threshold < 1 || ds->cfg.hard_threshold > 10) { set_error(error, error_size, "hard_threshold must be between 1 and 10"); return 0; }
    if (ds->cfg.buffer < 1.0 || ds->cfg.buffer > 5.0) { set_error(error, error_size, "buffer must be between 1 and 5"); return 0; }
    if (ds->cfg.review_reserve < 0.0 || ds->cfg.review_reserve >= 0.8) { set_error(error, error_size, "review_reserve must be between 0 and 0.8"); return 0; }
    if (ds->cfg.mastery_threshold < 0.0 || ds->cfg.mastery_threshold > 1.0) { set_error(error, error_size, "mastery_threshold must be between 0 and 1"); return 0; }
    if (n > 200000) { set_error(error, error_size, "too many topics (maximum 200000)"); return 0; }

    for (i = 0; i < n; ++i) {
        const Topic *t = &ds->topics[i];
        if (t->id != (int)i) { snprintf(error, error_size, "topic IDs must be contiguous and start at 0 (expected ID %zu)", i); return 0; }
        if (!t->name[0]) { snprintf(error, error_size, "topic %d has an empty name", t->id); return 0; }
        if (t->units < 1 || t->units > 1000000) { snprintf(error, error_size, "topic %d units must be between 1 and 1000000", t->id); return 0; }
        if (t->difficulty < 1 || t->difficulty > 10) { snprintf(error, error_size, "topic %d difficulty must be between 1 and 10", t->id); return 0; }
        if (t->value < 0 || t->value > 100) { snprintf(error, error_size, "topic %d value must be between 0 and 100", t->id); return 0; }
        if (t->mastery < 0 || t->mastery > 1) { snprintf(error, error_size, "topic %d mastery must be between 0 and 1", t->id); return 0; }
    }
    for (i = 0; i < ds->prereq_count; ++i) {
        int a = ds->prereqs[i].prereq, b = ds->prereqs[i].topic;
        if (a < 0 || b < 0 || (size_t)a >= n || (size_t)b >= n || a == b) {
            snprintf(error, error_size, "prerequisite row %zu references an invalid topic", i + 2); return 0;
        }
    }
    for (i = 0; i < ds->related_count; ++i) {
        int a = ds->related[i].a, b = ds->related[i].b;
        if (a < 0 || b < 0 || (size_t)a >= n || (size_t)b >= n || a == b || ds->related[i].effort < 0) {
            snprintf(error, error_size, "related-topic row %zu is invalid", i + 2); return 0;
        }
    }
    for (i = 0; i < ds->assessment_count; ++i) {
        const Assessment *a = &ds->assessments[i];
        int j;
        if (a->start < 0 || a->end <= a->start || a->weight < 0 || a->weight > 1 || (a->optional != 0 && a->optional != 1)) {
            snprintf(error, error_size, "assessment %d has an invalid time, weight, or optional flag", a->id); return 0;
        }
        for (j = 0; j < a->topic_count; ++j) {
            if (a->topic_ids[j] < 0 || (size_t)a->topic_ids[j] >= n) {
                snprintf(error, error_size, "assessment %d references unknown topic %d", a->id, a->topic_ids[j]); return 0;
            }
        }
    }
    for (i = 0; i < ds->mentor_count; ++i) {
        int skill;
        for (skill = 0; skill < ds->mentors[i].skill_count; ++skill) {
            int topic_id = ds->mentors[i].skills[skill];
            if (topic_id < 0 || (size_t)topic_id >= n) {
                snprintf(error, error_size, "mentor %d references unknown topic %d", ds->mentors[i].id, topic_id); return 0;
            }
        }
    }
    for (int missed_index = 0; missed_index < ds->cfg.missed_day_count; ++missed_index) {
        if (ds->cfg.missed_days[missed_index] < 0 || ds->cfg.missed_days[missed_index] >= ds->cfg.days) {
            set_error(error, error_size, "missed_days contains a day outside the planning horizon"); return 0;
        }
    }

    indegree = (int *)calloc(n, sizeof(int));
    queue = (int *)malloc(n * sizeof(int));
    if (!indegree || !queue) { free(indegree); free(queue); set_error(error, error_size, "out of memory validating prerequisite graph"); return 0; }
    for (i = 0; i < ds->prereq_count; ++i) indegree[ds->prereqs[i].topic]++;
    for (i = 0; i < n; ++i) if (indegree[i] == 0) queue[tail++] = (int)i;
    while (head < tail) {
        int u = queue[head++];
        size_t e;
        ++processed;
        for (e = 0; e < ds->prereq_count; ++e) {
            if (ds->prereqs[e].prereq == u) {
                int v = ds->prereqs[e].topic;
                if (--indegree[v] == 0) queue[tail++] = v;
            }
        }
    }
    free(indegree); free(queue);
    if (processed != n) { set_error(error, error_size, "prerequisite graph contains a cycle"); return 0; }
    return 1;
}

void dataset_summary(const Dataset *ds, FILE *out) {
    int mastered = 0;
    size_t i;
    for (i = 0; i < ds->topic_count; ++i) if (ds->topics[i].mastery >= ds->cfg.mastery_threshold) ++mastered;
    fprintf(out, "Dataset summary\n");
    fprintf(out, "  Type: %s\n", ds->cfg.kind);
    fprintf(out, "  Topics: %zu (%d already mastered)\n", ds->topic_count, mastered);
    fprintf(out, "  Prerequisite links: %zu\n", ds->prereq_count);
    fprintf(out, "  Assessments/deadlines: %zu\n", ds->assessment_count);
    fprintf(out, "  Mentors: %zu\n", ds->mentor_count);
    fprintf(out, "  Related-topic links: %zu\n", ds->related_count);
    fprintf(out, "  Horizon: %d days, capacity: %d units/day (1 unit = 30 minutes)\n", ds->cfg.days, ds->cfg.units_per_day);
}





#include <math.h>
#include <stdlib.h>
#include <string.h>

static int is_missed(const Dataset *ds, int day) {
    int i;
    for (i = 0; i < ds->cfg.missed_day_count; ++i) if (ds->cfg.missed_days[i] == day) return 1;
    return 0;
}

static int day_capacity(const Dataset *ds) {
    double usable = (double)ds->cfg.units_per_day * (1.0 - ds->cfg.review_reserve);
    int result = (int)floor(usable + 1e-9);
    return result < 1 ? 1 : result;
}

static int push_session(Plan *plan, int day, int topic, int units, int review) {
    Session *next;
    size_t capacity;
    if (units <= 0) return 1;
    if (plan->session_count == plan->session_capacity) {
        capacity = plan->session_capacity ? plan->session_capacity * 2 : 32;
        if (capacity < plan->session_capacity || capacity > ((size_t)-1) / sizeof(Session)) return 0;
        next = (Session *)realloc(plan->sessions, capacity * sizeof(Session));
        if (!next) return 0;
        plan->sessions = next;
        plan->session_capacity = capacity;
    }
    plan->sessions[plan->session_count].day = day;
    plan->sessions[plan->session_count].topic = topic;
    plan->sessions[plan->session_count].units = units;
    plan->sessions[plan->session_count].review = review;
    ++plan->session_count;
    if (review) plan->review_units += units;
    else plan->study_units += units;
    plan->total_units += units;
    return 1;
}

static int build_topological_order(const Dataset *ds, int *order) {
    int *indegree;
    int *queue;
    size_t n = ds->topic_count, i;
    int head = 0, tail = 0, used = 0;
    indegree = (int *)calloc(n, sizeof(int));
    queue = (int *)malloc(n * sizeof(int));
    if (!indegree || !queue) { free(indegree); free(queue); return 0; }
    for (i = 0; i < ds->prereq_count; ++i) indegree[ds->prereqs[i].topic]++;
    for (i = 0; i < n; ++i) if (indegree[i] == 0) queue[tail++] = (int)i;
    while (head < tail) {
        int u = queue[head++];
        size_t e;
        order[used++] = u;
        for (e = 0; e < ds->prereq_count; ++e) {
            if (ds->prereqs[e].prereq == u) {
                int v = ds->prereqs[e].topic;
                if (--indegree[v] == 0) queue[tail++] = v;
            }
        }
    }
    free(indegree); free(queue);
    return used == (int)n;
}

static int topic_deadlines(const Dataset *ds, int *deadline, double *exam_weight) {
    size_t i, j, n = ds->topic_count;
    for (i = 0; i < n; ++i) { deadline[i] = ds->cfg.days - 1; exam_weight[i] = 0.05; }
    for (i = 0; i < ds->assessment_count; ++i) {
        const Assessment *a = &ds->assessments[i];
        long long day_ll = a->start / 1440LL;
        int last_study_day = (int)day_ll - 1;
        if (last_study_day >= ds->cfg.days) last_study_day = ds->cfg.days - 1;
        for (j = 0; j < (size_t)a->topic_count; ++j) {
            int t = a->topic_ids[j];
            if (last_study_day < deadline[t]) deadline[t] = last_study_day;
            exam_weight[t] += a->weight;
        }
    }
    for (i = 0; i < n; ++i) if (exam_weight[i] > 1.0) exam_weight[i] = 1.0;
    return 1;
}

static int topic_urgent(const Dataset *ds, int deadline) {
    int window = ds->cfg.days / 3;
    if (window < 2) window = 2;
    if (window > 4) window = 4;
    return deadline <= window;
}

static int topic_quadrant(const Dataset *ds, const Topic *topic, int deadline) {
    int important = topic->value >= 50.0;
    int urgent = topic_urgent(ds, deadline);
    if (important && urgent) return 1;
    if (important && !urgent) return 2;
    if (!important && urgent) return 3;
    return 4;
}

static double topic_priority(const Topic *t, int deadline, double weight) {
    int days_left = deadline + 1;
    double urgency, priority;
    if (days_left < 1) days_left = 1;
    urgency = 1.0 + 3.0 / (double)days_left;
    priority = (t->value + 1.0) * (1.0 - t->mastery) * (0.35 + 2.0 * weight);
    priority *= (1.0 + (double)(t->difficulty - 1) * 0.025);
    priority *= urgency;
    return priority;
}

static int selection_fits(const Dataset *ds, const int *selected, const int *deadline,
                          const int *cost, const int *hard, const int *add, int *prefix_cost,
                          int *prefix_hard) {
    int d, total = 0, total_hard = 0;
    int daily = day_capacity(ds);
    int *selected_by_deadline = prefix_cost;
    int *hard_by_deadline = prefix_hard;
    int *capacity_prefix;
    int *hard_capacity_prefix;
    int n = (int)ds->topic_count;
    int i;
    memset(selected_by_deadline, 0, (size_t)ds->cfg.days * sizeof(int));
    memset(hard_by_deadline, 0, (size_t)ds->cfg.days * sizeof(int));
    capacity_prefix = (int *)calloc((size_t)ds->cfg.days, sizeof(int));
    hard_capacity_prefix = (int *)calloc((size_t)ds->cfg.days, sizeof(int));
    if (!capacity_prefix || !hard_capacity_prefix) { free(capacity_prefix); free(hard_capacity_prefix); return 0; }
    for (i = 0; i < n; ++i) {
        if (selected[i] || add[i]) {
            int day = deadline[i];
            if (day < 0) { free(capacity_prefix); free(hard_capacity_prefix); return 0; }
            if (day >= ds->cfg.days) day = ds->cfg.days - 1;
            selected_by_deadline[day] += cost[i];
            total += cost[i];
            if (hard[i]) { hard_by_deadline[day] += cost[i]; total_hard += cost[i]; }
        }
    }
    for (d = 0; d < ds->cfg.days; ++d) {
        capacity_prefix[d] = (d ? capacity_prefix[d - 1] : 0) + (is_missed(ds, d) ? 0 : daily);
        hard_capacity_prefix[d] = (d ? hard_capacity_prefix[d - 1] : 0) + (is_missed(ds, d) ? 0 : ds->cfg.hard_units_per_day);
        if (selected_by_deadline[d] || hard_by_deadline[d]) {

        }
    }
    for (d = 1; d < ds->cfg.days; ++d) {
        selected_by_deadline[d] += selected_by_deadline[d - 1];
        hard_by_deadline[d] += hard_by_deadline[d - 1];
    }
    for (d = 0; d < ds->cfg.days; ++d) {
        if (selected_by_deadline[d] > capacity_prefix[d] || hard_by_deadline[d] > hard_capacity_prefix[d]) {
            free(capacity_prefix); free(hard_capacity_prefix); return 0;
        }
    }
    free(capacity_prefix); free(hard_capacity_prefix);
    if (total > (ds->cfg.days * daily) || total_hard > (ds->cfg.days * ds->cfg.hard_units_per_day)) return 0;
    return 1;
}

static void mark_ancestors(const Dataset *ds, int topic, const int *mastered, int *needed) {
    int changed = 1;
    needed[topic] = 1;
    while (changed) {
        size_t i;
        changed = 0;
        for (i = 0; i < ds->prereq_count; ++i) {
            int p = ds->prereqs[i].prereq;
            int t = ds->prereqs[i].topic;
            if (needed[t] && !mastered[p] && !needed[p]) { needed[p] = 1; changed = 1; }
        }
    }
}

static int attempt_select(const Dataset *ds, int candidate, const int *mastered, const int *order,
                          const int *deadline, const int *cost, const int *hard, int *selected,
                          int *needed, int *scratch_a, int *scratch_b, int matrix) {
    int i;
    memset(needed, 0, ds->topic_count * sizeof(int));
    mark_ancestors(ds, candidate, mastered, needed);
    for (i = 0; i < (int)ds->topic_count; ++i) {
        int t = order[i];
        if (needed[t] && !selected[t] && !mastered[t]) {
            if (deadline[t] < 0) return 0;
            if (matrix && t == candidate && topic_quadrant(ds, &ds->topics[t], deadline[t]) == 4) return 0;
        }
    }
    if (!selection_fits(ds, selected, deadline, cost, hard, needed, scratch_a, scratch_b)) return 0;
    for (i = 0; i < (int)ds->topic_count; ++i) {
        int t = order[i];
        if (needed[t] && !mastered[t]) selected[t] = 1;
    }
    return 1;
}


static void sort_candidates(int *items, int count, const double *score) {
    int i;
    for (i = 1; i < count; ++i) {
        int value = items[i], j = i;
        while (j > 0 && (score[items[j - 1]] < score[value] ||
               (score[items[j - 1]] == score[value] && items[j - 1] > value))) {
            items[j] = items[j - 1];
            --j;
        }
        items[j] = value;
    }
}

const char *strategy_name(Strategy strategy) {
    return strategy == STRATEGY_BASELINE ? "baseline" : "greedy";
}

int strategy_parse(const char *text, Strategy *strategy) {
    if (strcmp(text, "baseline") == 0) { *strategy = STRATEGY_BASELINE; return 1; }
    if (strcmp(text, "greedy") == 0) { *strategy = STRATEGY_GREEDY; return 1; }
    return 0;
}

int plan_build(const Dataset *ds, Strategy strategy, int use_eisenhower, Plan *plan, char *error, size_t error_size) {
    int n = (int)ds->topic_count, i, j, daily;
    int *order = NULL, *deadline = NULL, *cost = NULL, *hard = NULL;
    int *mastered = NULL, *needed = NULL, *scratch_a = NULL, *scratch_b = NULL;
    int *candidates = NULL, candidate_count = 0;
    double *weights = NULL, *scores = NULL;
    int *remaining = NULL, *used_by_day = NULL;
    int success = 0;
    memset(plan, 0, sizeof(*plan));
    plan->strategy = strategy;
    order = (int *)malloc((size_t)n * sizeof(int));
    deadline = (int *)malloc((size_t)n * sizeof(int));
    cost = (int *)malloc((size_t)n * sizeof(int));
    hard = (int *)calloc((size_t)n, sizeof(int));
    mastered = (int *)calloc((size_t)n, sizeof(int));
    needed = (int *)calloc((size_t)n, sizeof(int));
    scratch_a = (int *)calloc((size_t)ds->cfg.days, sizeof(int));
    scratch_b = (int *)calloc((size_t)ds->cfg.days, sizeof(int));
    candidates = (int *)malloc((size_t)n * sizeof(int));
    weights = (double *)malloc((size_t)n * sizeof(double));
    scores = (double *)calloc((size_t)n, sizeof(double));
    plan->selected = (int *)calloc((size_t)n, sizeof(int));
    plan->completion_day = (int *)malloc((size_t)n * sizeof(int));
    remaining = (int *)calloc((size_t)n, sizeof(int));
    used_by_day = (int *)calloc((size_t)ds->cfg.days, sizeof(int));
    if (!order || !deadline || !cost || !hard || !mastered || !needed || !scratch_a || !scratch_b ||
        !candidates || !weights || !scores || !plan->selected || !plan->completion_day || !remaining || !used_by_day) {
        snprintf(error, error_size, "out of memory preparing the plan"); goto cleanup;
    }
    for (i = 0; i < n; ++i) plan->completion_day[i] = -1;
    if (!build_topological_order(ds, order)) { snprintf(error, error_size, "could not sort prerequisite graph"); goto cleanup; }
    topic_deadlines(ds, deadline, weights);
    daily = day_capacity(ds);
    for (i = 0; i < n; ++i) {
        const Topic *t = &ds->topics[i];
        mastered[i] = t->mastery >= ds->cfg.mastery_threshold;
        cost[i] = (int)ceil((double)t->units * ds->cfg.buffer - 1e-9);
        if (cost[i] < 1) cost[i] = 1;
        hard[i] = t->difficulty >= ds->cfg.hard_threshold;
        if (!mastered[i]) {
            candidates[candidate_count++] = i;
            scores[i] = topic_priority(t, deadline[i], weights[i]);
            if (use_eisenhower) {
                int q = topic_quadrant(ds, t, deadline[i]);
                if (q == 1) scores[i] *= 1.35;
                else if (q == 3) scores[i] *= 0.80;
                else if (q == 4) scores[i] *= 0.20;
            }
        }
    }
    if (strategy == STRATEGY_BASELINE) {
        candidate_count = 0;
        for (i = 0; i < n; ++i) if (!mastered[order[i]]) candidates[candidate_count++] = order[i];
    } else {
        sort_candidates(candidates, candidate_count, scores);
    }
    for (j = 0; j < candidate_count; ++j) {
        int t = candidates[j];
        if (t < 0 || t >= n || mastered[t] || plan->selected[t]) continue;
        if (use_eisenhower && topic_quadrant(ds, &ds->topics[t], deadline[t]) == 4) continue;
        (void)attempt_select(ds, t, mastered, order, deadline, cost, hard, plan->selected,
                             needed, scratch_a, scratch_b, use_eisenhower);
    }



    for (i = 0; i < n; ++i) if (plan->selected[i]) remaining[i] = cost[i];
    for (int day = 0; day < ds->cfg.days; ++day) {
        int free_units = is_missed(ds, day) ? 0 : daily;
        int hard_left = is_missed(ds, day) ? 0 : ds->cfg.hard_units_per_day;
        while (free_units > 0) {
            int best = -1;
            double best_score = -1.0;
            int t;
            for (t = 0; t < n; ++t) {
                int ready = 1;
                size_t e;
                if (!plan->selected[t] || remaining[t] <= 0 || deadline[t] < day) continue;
                for (e = 0; e < ds->prereq_count; ++e) if (ds->prereqs[e].topic == t) {
                    int p = ds->prereqs[e].prereq;
                    if (!mastered[p] && (plan->completion_day[p] < 0 || plan->completion_day[p] >= day)) { ready = 0; break; }
                }
                if (!ready || (hard[t] && hard_left <= 0)) continue;
                if (best < 0 || deadline[t] < deadline[best] ||
                    (deadline[t] == deadline[best] && scores[t] > best_score)) {
                    best = t; best_score = scores[t];
                }
            }
            if (best < 0) break;
            {
                int take = remaining[best];
                if (take > free_units) take = free_units;
                if (hard[best] && take > hard_left) take = hard_left;
                if (take < 1) break;
                if (!push_session(plan, day, best, take, 0)) { snprintf(error, error_size, "out of memory adding study sessions"); goto cleanup; }
                remaining[best] -= take;
                free_units -= take;
                if (hard[best]) hard_left -= take;
                used_by_day[day] += take;
                if (remaining[best] == 0) plan->completion_day[best] = day;
            }
        }
    }


    for (i = 0; i < n; ++i) {
        int t = order[i];
        size_t e;
        int invalid = plan->selected[t] && (remaining[t] > 0 || plan->completion_day[t] < 0 || plan->completion_day[t] > deadline[t]);
        if (invalid) plan->selected[t] = 0;
        if (plan->selected[t]) {
            for (e = 0; e < ds->prereq_count; ++e) if (ds->prereqs[e].topic == t) {
                int p = ds->prereqs[e].prereq;
                if (!mastered[p] && !plan->selected[p]) { plan->selected[t] = 0; break; }
            }
        }
    }

    {
        size_t read_index, write_index = 0;
        plan->study_units = 0;
        plan->total_units = plan->review_units;
        for (read_index = 0; read_index < plan->session_count; ++read_index) {
            Session s = plan->sessions[read_index];
            if (s.review || plan->selected[s.topic]) {
                plan->sessions[write_index++] = s;
                if (!s.review) { plan->study_units += s.units; plan->total_units += s.units; }
            } else {
                used_by_day[s.day] -= s.units;
            }
        }
        plan->session_count = write_index;
    }

    for (i = 0; i < n; ++i) if (plan->selected[i] && plan->completion_day[i] >= 0) {
        int g;
        for (g = 0; g < ds->cfg.review_gap_count; ++g) {
            int day = plan->completion_day[i] + ds->cfg.review_gaps[g];
            if (day >= ds->cfg.days || is_missed(ds, day)) continue;
            if (used_by_day[day] < ds->cfg.units_per_day) {
                if (!push_session(plan, day, i, 1, 1)) { snprintf(error, error_size, "out of memory adding review sessions"); goto cleanup; }
                ++used_by_day[day];
            }
        }
    }
    for (i = 0; i < n; ++i) if (plan->selected[i]) ++plan->planned_topics;
    plan->missed_days = ds->cfg.missed_day_count;
    success = 1;
cleanup:
    free(order); free(deadline); free(cost); free(hard); free(mastered); free(needed);
    free(scratch_a); free(scratch_b); free(candidates); free(weights); free(scores);
    free(remaining); free(used_by_day);
    if (!success) plan_free(plan);
    return success;
}

void plan_free(Plan *plan) {
    if (!plan) return;
    free(plan->selected);
    free(plan->completion_day);
    free(plan->sessions);
    memset(plan, 0, sizeof(*plan));
}

void plan_print(const Dataset *ds, const Plan *plan, FILE *out, int max_sessions) {
    int i, shown = 0;
    int *per_day = (int *)calloc((size_t)ds->cfg.days, sizeof(int));
    int *review_per_day = (int *)calloc((size_t)ds->cfg.days, sizeof(int));
    size_t s;
    fprintf(out, "Plan strategy: %s%s\n", strategy_name(plan->strategy), plan->planned_topics == 0 ? " (no topics fit)" : "");
    fprintf(out, "Selected topics: %d | Study units: %d | Review units: %d | Total: %d units (%.1f hours)\n",
            plan->planned_topics, plan->study_units, plan->review_units, plan->total_units, plan->total_units * 0.5);
    fprintf(out, "\nDay-by-day agenda\n");
    for (s = 0; s < plan->session_count; ++s) {
        if (plan->sessions[s].review) review_per_day[plan->sessions[s].day] += plan->sessions[s].units;
        else per_day[plan->sessions[s].day] += plan->sessions[s].units;
    }
    for (i = 0; i < ds->cfg.days; ++i) {
        int study = per_day ? per_day[i] : 0, review = review_per_day ? review_per_day[i] : 0;
        if (study + review || is_missed(ds, i)) {
            fprintf(out, "  Day %2d: ", i);
            if (is_missed(ds, i)) fprintf(out, "MISSED");
            else fprintf(out, "%d study + %d review unit(s)", study, review);
            fputc('\n', out);
        }
    }
    fprintf(out, "\nSessions\n");
    for (s = 0; s < plan->session_count; ++s) {
        const Session *session = &plan->sessions[s];
        if (max_sessions >= 0 && shown >= max_sessions) break;
        fprintf(out, "  Day %2d | %-6s | %2d unit(s) | %s\n", session->day,
                session->review ? "Review" : "Study", session->units, ds->topics[session->topic].name);
        ++shown;
    }
    if (shown < (int)plan->session_count) fprintf(out, "  ... %zu more session(s) not shown\n", plan->session_count - (size_t)shown);
    fprintf(out, "\nTopics selected\n");
    for (i = 0; i < (int)ds->topic_count; ++i) if (plan->selected[i]) {
        fprintf(out, "  [%2d] %-34s complete by day %d\n", ds->topics[i].id, ds->topics[i].name, plan->completion_day[i]);
    }
    free(per_day); free(review_per_day);
}

void matrix_print(const Dataset *ds, FILE *out, int show_all) {
    static const char *names[] = {"", "DO", "DECIDE", "DELEGATE", "DELETE"};
    int q, i, deadline;
    int *deadlines = (int *)malloc(ds->topic_count * sizeof(int));
    double *weights = (double *)malloc(ds->topic_count * sizeof(double));
    if (!deadlines || !weights) { free(deadlines); free(weights); fprintf(out, "Unable to allocate matrix.\n"); return; }
    topic_deadlines(ds, deadlines, weights);
    fprintf(out, "Eisenhower matrix (importance threshold: 50/100; urgent: within roughly one-third of the horizon)\n");
    for (q = 1; q <= 4; ++q) {
        fprintf(out, "\n%s\n", names[q]);
        for (i = 0; i < (int)ds->topic_count; ++i) {
            deadline = deadlines[i];
            if (topic_quadrant(ds, &ds->topics[i], deadline) == q &&
                (show_all || ds->topics[i].mastery < ds->cfg.mastery_threshold)) {
                fprintf(out, "  [%2d] %-36s importance %5.1f | deadline day %d\n",
                        ds->topics[i].id, ds->topics[i].name, ds->topics[i].value, deadline);
            }
        }
    }
    free(deadlines); free(weights);
}

int plan_export_csv(const Dataset *ds, const Plan *plan, const char *path, char *error, size_t error_size) {
    FILE *f = fopen(path, "w");
    size_t i;
    if (!f) { snprintf(error, error_size, "cannot write %s", path); return 0; }
    fputs("day,topic_id,topic,session_type,units\n", f);
    for (i = 0; i < plan->session_count; ++i) {
        const Session *s = &plan->sessions[i];
        char escaped[LP_NAME_LEN * 2];
        size_t j, k = 0;
        for (j = 0; ds->topics[s->topic].name[j] && k + 2 < sizeof(escaped); ++j) {
            if (ds->topics[s->topic].name[j] == '"') escaped[k++] = '"';
            escaped[k++] = ds->topics[s->topic].name[j];
        }
        escaped[k] = '\0';
        fprintf(f, "%d,%d,\"%s\",%s,%d\n", s->day, s->topic,
                escaped, s->review ? "review" : "study", s->units);
    }
    if (fclose(f) != 0) { snprintf(error, error_size, "failed while writing %s", path); return 0; }
    return 1;
}

int plan_export_json(const Dataset *ds, const Plan *plan, const Simulation *sim, const char *path, char *error, size_t error_size) {
    FILE *f = fopen(path, "w");
    size_t i;
    if (!f) { snprintf(error, error_size, "cannot write %s", path); return 0; }
    fprintf(f, "{\n  \"strategy\": \"%s\",\n  \"planned_topics\": %d,\n  \"study_units\": %d,\n  \"review_units\": %d,\n  \"sessions\": [\n",
            strategy_name(plan->strategy), plan->planned_topics, plan->study_units, plan->review_units);
    for (i = 0; i < plan->session_count; ++i) {
        const Session *s = &plan->sessions[i];
        fprintf(f, "    {\"day\": %d, \"topic_id\": %d, \"topic\": \"", s->day, s->topic);
        for (const char *p = ds->topics[s->topic].name; *p; ++p) {
            if (*p == '"' || *p == '\\') fputc('\\', f);
            if ((unsigned char)*p >= 32) fputc(*p, f);
        }
        fprintf(f, "\", \"type\": \"%s\", \"units\": %d}%s\n", s->review ? "review" : "study", s->units,
                i + 1 == plan->session_count ? "" : ",");
    }
    fprintf(f, "  ]");
    if (sim) fprintf(f, ",\n  \"weighted_exam_score\": %.4f,\n  \"simulated_study_units\": %d,\n  \"simulated_review_units\": %d\n", sim->weighted_score, sim->study_units, sim->review_units);
    else fputc('\n', f);
    fputs("}\n", f);
    if (fclose(f) != 0) { snprintf(error, error_size, "failed while writing %s", path); return 0; }
    return 1;
}





#include <math.h>
#include <stdlib.h>
#include <string.h>

#define COMPLETION_GAIN 0.85
#define BASE_STABILITY 3.0
#define KNOWN_STABILITY 10.0
#define STUDY_STABILITY_MULT 1.5
#define REVIEW_STABILITY_MULT 2.0
#define REVIEW_GAIN 0.15
#define FATIGUE_FREE_UNITS 6
#define FATIGUE_FACTOR 0.75

static int day_is_missed(const Dataset *ds, int day, const int *extra, int extra_count) {
    int i;
    for (i = 0; i < ds->cfg.missed_day_count; ++i) if (ds->cfg.missed_days[i] == day) return 1;
    for (i = 0; i < extra_count; ++i) if (extra[i] == day) return 1;
    return 0;
}

static double retention(double stability, int days) {
    if (days <= 0) return 1.0;
    if (stability < 0.1) stability = 0.1;
    return exp(-(double)days / stability);
}

static double effective_mastery(double mastery, double stability, int last_day, int day) {
    return mastery * retention(stability, day - last_day);
}

static int valid_missed_days(const Dataset *ds, const int *extra, int count) {
    int i;
    for (i = 0; i < count; ++i) if (extra[i] < 0 || extra[i] >= ds->cfg.days) return 0;
    return 1;
}

int simulation_run(const Dataset *ds, const Plan *plan, const int *extra_missed, int extra_missed_count, Simulation *sim) {
    size_t n = ds->topic_count, i;
    double *progress = NULL;
    double *stored = NULL;
    double *stability = NULL;
    int *last = NULL;
    int *completed = NULL;
    int day, executed_study = 0, executed_review = 0, skipped = 0;
    double weighted_sum = 0.0, weight_total = 0.0;
    memset(sim, 0, sizeof(*sim));
    if (!valid_missed_days(ds, extra_missed, extra_missed_count)) return 0;
    progress = (double *)calloc(n, sizeof(double));
    stored = (double *)calloc(n, sizeof(double));
    stability = (double *)malloc(n * sizeof(double));
    last = (int *)calloc(n, sizeof(int));
    completed = (int *)calloc(n, sizeof(int));
    sim->mastery = (double *)calloc(n, sizeof(double));
    sim->stability = (double *)calloc(n, sizeof(double));
    sim->day_log = (char *)malloc((size_t)ds->cfg.days + 1);
    sim->assessment_scores = (double *)calloc(ds->assessment_count ? ds->assessment_count : 1, sizeof(double));
    sim->assessment_count = ds->assessment_count;
    if (!progress || !stored || !stability || !last || !completed ||
        !sim->mastery || !sim->stability || !sim->day_log || !sim->assessment_scores) goto fail;
    for (i = 0; i < n; ++i) {
        stored[i] = ds->topics[i].mastery;
        stability[i] = BASE_STABILITY + (KNOWN_STABILITY - BASE_STABILITY) * ds->topics[i].mastery;
        last[i] = 0;
        completed[i] = ds->topics[i].mastery >= ds->cfg.mastery_threshold;
    }
    for (day = 0; day < ds->cfg.days; ++day) {
        int any_study = 0, any_review = 0, total_today = 0;
        int missed = day_is_missed(ds, day, extra_missed, extra_missed_count);
        if (missed) {
            sim->day_log[day] = 'M';
            for (i = 0; i < plan->session_count; ++i) if (plan->sessions[i].day == day) skipped += plan->sessions[i].units;
            continue;
        }
        for (i = 0; i < plan->session_count; ++i) {
            const Session *s = &plan->sessions[i];
            int t = s->topic;
            if (s->day != day) continue;
            if (s->review) {
                double current = effective_mastery(stored[t], stability[t], last[t], day);
                stored[t] = current + (1.0 - current) * REVIEW_GAIN;
                stability[t] *= REVIEW_STABILITY_MULT;
                last[t] = day;
                executed_review += s->units;
                any_review = 1;
            } else {
                int u;
                double effective_units = 0.0;
                for (u = 0; u < s->units; ++u) effective_units += (total_today + u < FATIGUE_FREE_UNITS) ? 1.0 : FATIGUE_FACTOR;
                {
                    double current = effective_mastery(stored[t], stability[t], last[t], day);
                    double close = 1.0 - pow(1.0 - COMPLETION_GAIN, effective_units / (double)ds->topics[t].units);
                    stored[t] = current + (1.0 - current) * close;
                }
                last[t] = day;
                progress[t] += effective_units;
                if (!completed[t] && progress[t] >= (double)ds->topics[t].units) {
                    completed[t] = 1;
                    stability[t] *= STUDY_STABILITY_MULT;
                }
                total_today += s->units;
                executed_study += s->units;
                any_study = 1;
            }
        }
        sim->day_log[day] = any_study && any_review ? 'B' : any_study ? 'S' : any_review ? 'R' : '.';
    }
    sim->day_log[ds->cfg.days] = '\0';
    for (i = 0; i < n; ++i) {
        sim->mastery[i] = effective_mastery(stored[i], stability[i], last[i], ds->cfg.days - 1);
        sim->stability[i] = stability[i];
    }
    if (ds->assessment_count) {
        for (i = 0; i < ds->assessment_count; ++i) {
            const Assessment *a = &ds->assessments[i];
            double sum = 0.0;
            int j, exam_day = (int)(a->start / 1440LL);
            for (j = 0; j < a->topic_count; ++j) {
                int t = a->topic_ids[j];
                double m = effective_mastery(stored[t], stability[t], last[t], exam_day);
                sum += m;
            }
            sim->assessment_scores[i] = a->topic_count ? sum / (double)a->topic_count : 0.0;
            weighted_sum += sim->assessment_scores[i] * a->weight;
            weight_total += a->weight;
        }
        sim->weighted_score = weight_total > 0 ? weighted_sum / weight_total : 0.0;
    } else {
        double sum = 0.0;
        for (i = 0; i < n; ++i) sum += sim->mastery[i];
        sim->weighted_score = n ? sum / (double)n : 0.0;
    }
    sim->study_units = executed_study;
    sim->review_units = executed_review;
    sim->skipped_units = skipped;
    free(progress); free(stored); free(stability); free(last); free(completed);
    return 1;
fail:
    free(progress); free(stored); free(stability); free(last); free(completed);
    simulation_free(sim);
    return 0;
}

void simulation_free(Simulation *sim) {
    if (!sim) return;
    free(sim->mastery);
    free(sim->stability);
    free(sim->assessment_scores);
    free(sim->day_log);
    memset(sim, 0, sizeof(*sim));
}

void simulation_print(const Dataset *ds, const Simulation *sim, FILE *out) {
    size_t i;
    fprintf(out, "\nLearning simulation\n");
    fprintf(out, "  Day log: %s  (S=study, R=review, B=both, M=missed, .=idle)\n", sim->day_log ? sim->day_log : "");
    fprintf(out, "  Executed: %d study + %d review units; skipped: %d units\n", sim->study_units, sim->review_units, sim->skipped_units);
    if (ds->assessment_count) {
        for (i = 0; i < ds->assessment_count; ++i) {
            fprintf(out, "  %-30s expected mastery: %5.1f%%\n", ds->assessments[i].name, sim->assessment_scores[i] * 100.0);
        }
        fprintf(out, "  Weighted expected assessment score: %.1f%%\n", sim->weighted_score * 100.0);
    } else {
        fprintf(out, "  Average retained mastery at horizon end: %.1f%%\n", sim->weighted_score * 100.0);
    }
}





#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int has_flag(int argc, char **argv, const char *flag) {
    int i;
    for (i = 2; i < argc; ++i) if (strcmp(argv[i], flag) == 0) return 1;
    return 0;
}

static const char *option_value(int argc, char **argv, const char *option) {
    int i;
    for (i = 2; i + 1 < argc; ++i) if (strcmp(argv[i], option) == 0) return argv[i + 1];
    return NULL;
}

static const char *first_directory(int argc, char **argv) {
    int i;
    for (i = 2; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--matrix") == 0 || strcmp(argv[i], "--all") == 0) continue;
            if (i + 1 < argc) ++i;
            continue;
        }
        return argv[i];
    }
    return NULL;
}

static int parse_missed(const char *text, int *days, int max_days) {
    const char *cursor = text;
    int count = 0;
    if (!text || !*text) return 0;
    while (*cursor) {
        const char *separator = strchr(cursor, ',');
        size_t length = separator ? (size_t)(separator - cursor) : strlen(cursor);
        char token[64];
        char *end = NULL;
        long value;
        if (length == 0 || length >= sizeof(token) || count >= max_days) return -1;
        memcpy(token, cursor, length);
        token[length] = '\0';
        value = strtol(token, &end, 10);
        while (end && isspace((unsigned char)*end)) ++end;
        if (end == token || (end && *end) || value < 0 || value > 100000) return -1;
        days[count++] = (int)value;
        if (!separator) break;
        cursor = separator + 1;
        if (!*cursor) return -1;
    }
    return count;
}

static int load_and_validate(const char *directory, Dataset *ds) {
    char error[1024];
    if (!dataset_load(directory, ds, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        return 0;
    }
    if (!dataset_validate(ds, error, sizeof(error))) {
        fprintf(stderr, "error: invalid dataset: %s\n", error);
        dataset_free(ds);
        return 0;
    }
    return 1;
}

static int command_validate(const char *directory) {
    Dataset ds;
    if (!load_and_validate(directory, &ds)) return 1;
    printf("Dataset OK\n\n");
    dataset_summary(&ds, stdout);
    dataset_free(&ds);
    return 0;
}

static int command_plan(int argc, char **argv, int simulation_only) {
    const char *directory = first_directory(argc, argv);
    if (!directory) directory = "builtin";
    const char *strategy_text = option_value(argc, argv, "--strategy");
    const char *csv_path = option_value(argc, argv, "--csv");
    const char *json_path = option_value(argc, argv, "--json");
    const char *missed_text = option_value(argc, argv, "--missed");
    const char *lines_text = option_value(argc, argv, "--lines");
    Strategy strategy = STRATEGY_GREEDY;
    Dataset ds;
    Plan plan;
    Simulation sim;
    int missed[LP_MAX_MISSED_DAYS];
    int missed_count = 0;
    int max_lines = simulation_only ? 0 : 40;
    char error[1024];
    if (!directory) { fprintf(stderr, "error: missing dataset directory\n"); return 1; }
    if (strategy_text && !strategy_parse(strategy_text, &strategy)) {
        fprintf(stderr, "error: strategy must be 'baseline' or 'greedy'\n"); return 1;
    }
    if (lines_text) {
        char *end = NULL;
        long value = strtol(lines_text, &end, 10);
        if (!end || *end || value < 0 || value > 100000) { fprintf(stderr, "error: --lines must be between 0 and 100000\n"); return 1; }
        max_lines = (int)value;
    }
    missed_count = parse_missed(missed_text, missed, LP_MAX_MISSED_DAYS);
    if (missed_count < 0) { fprintf(stderr, "error: --missed must be a comma-separated list such as 2,3,4\n"); return 1; }
    if (!load_and_validate(directory, &ds)) return 1;
    for (int i = 0; i < missed_count; ++i) if (missed[i] >= ds.cfg.days) {
        fprintf(stderr, "error: missed day %d is outside the %d-day horizon\n", missed[i], ds.cfg.days);
        dataset_free(&ds); return 1;
    }
    if (!plan_build(&ds, strategy, has_flag(argc, argv, "--matrix"), &plan, error, sizeof(error))) {
        fprintf(stderr, "error: cannot build plan: %s\n", error); dataset_free(&ds); return 1;
    }
    if (!simulation_run(&ds, &plan, missed, missed_count, &sim)) {
        fprintf(stderr, "error: could not run simulation (check missed days and available memory)\n");
        plan_free(&plan); dataset_free(&ds); return 1;
    }
    dataset_summary(&ds, stdout);
    putchar('\n');
    plan_print(&ds, &plan, stdout, max_lines);
    simulation_print(&ds, &sim, stdout);
    if (missed_count) printf("\nAdditional missed days simulated: %d\n", missed_count);
    if (has_flag(argc, argv, "--matrix")) {
        printf("\nEisenhower matrix was applied to topic selection.\n");
    }
    if (csv_path) {
        if (!plan_export_csv(&ds, &plan, csv_path, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error); simulation_free(&sim); plan_free(&plan); dataset_free(&ds); return 1;
        }
        printf("\nWrote CSV: %s\n", csv_path);
    }
    if (json_path) {
        if (!plan_export_json(&ds, &plan, &sim, json_path, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n", error); simulation_free(&sim); plan_free(&plan); dataset_free(&ds); return 1;
        }
        printf("Wrote JSON: %s\n", json_path);
    }
    simulation_free(&sim);
    plan_free(&plan);
    dataset_free(&ds);
    return 0;
}

static int command_compare(int argc, char **argv) {
    const char *directory = first_directory(argc, argv);
    if (!directory) directory = "builtin";
    Dataset ds;
    Plan baseline, greedy;
    Simulation sim_base, sim_greedy;
    char error[1024];
    int matrix = has_flag(argc, argv, "--matrix");
    if (!directory) { fprintf(stderr, "error: missing dataset directory\n"); return 1; }
    if (!load_and_validate(directory, &ds)) return 1;
    if (!plan_build(&ds, STRATEGY_BASELINE, matrix, &baseline, error, sizeof(error)) ||
        !plan_build(&ds, STRATEGY_GREEDY, matrix, &greedy, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error); dataset_free(&ds); return 1;
    }
    if (!simulation_run(&ds, &baseline, NULL, 0, &sim_base) || !simulation_run(&ds, &greedy, NULL, 0, &sim_greedy)) {
        fprintf(stderr, "error: simulation failed\n"); plan_free(&baseline); plan_free(&greedy); dataset_free(&ds); return 1;
    }
    dataset_summary(&ds, stdout);
    printf("\nStrategy comparison%s\n", matrix ? " (Eisenhower enabled)" : "");
    printf("  %-12s | %7s | %10s | %10s | %10s\n", "Strategy", "Topics", "Study units", "Review", "Exam score");
    printf("  -------------+---------+------------+------------+-----------\n");
    printf("  %-12s | %7d | %10d | %10d | %9.1f%%\n", "baseline", baseline.planned_topics,
           baseline.study_units, baseline.review_units, sim_base.weighted_score * 100.0);
    printf("  %-12s | %7d | %10d | %10d | %9.1f%%\n", "greedy", greedy.planned_topics,
           greedy.study_units, greedy.review_units, sim_greedy.weighted_score * 100.0);
    simulation_free(&sim_base); simulation_free(&sim_greedy);
    plan_free(&baseline); plan_free(&greedy); dataset_free(&ds);
    return 0;
}

static void command_algorithms(void) {
    puts("C edition: algorithm concepts used by the planner\n");
    puts("  1. Graphs / topological sorting: Kahn's algorithm validates prerequisite order.");
    puts("  2. Greedy optimisation: ranks topics by value, mastery gap, urgency and study cost.");
    puts("  3. Feasibility checks: prefix-capacity tests respect deadlines and hard-topic limits.");
    puts("  4. Scheduling: day-by-day list scheduling starts a topic after its prerequisites.");
    puts("  5. Simulation: exponential memory decay, spaced review and diminishing study returns.");
    puts("  6. Eisenhower matrix: classifies tasks as Do, Decide, Delegate or Delete.");
    puts("\nThe original C++ release also contained DP, branch-and-bound, flow, string matching,");
    puts("number theory and additional GUI/workbench demonstrations. Those are not in this C port.");
}

static int command_menu(void) {
    char line[128];
    for (;;) {
        puts("\nPERSONALIZED LEARNING PLANNER - C edition");
        puts("1. Validate sample dataset");
        puts("2. Generate a plan for sample dataset");
        puts("3. Show Eisenhower matrix for the built-in sample");
        puts("4. Compare planning strategies for sample dataset");
        puts("5. Exit");
        printf("Choose an option: ");
        if (!fgets(line, sizeof(line), stdin)) return 0;
        switch (atoi(line)) {
            case 1: command_validate("builtin"); break;
            case 2: { char *args[] = {"planner", "plan", "builtin"}; command_plan(3, args, 0); break; }
            case 3: { Dataset ds; if (load_and_validate("builtin", &ds)) { matrix_print(&ds, stdout, 1); dataset_free(&ds); } break; }
            case 4: { char *args[] = {"planner", "compare", "builtin"}; command_compare(3, args); break; }
            case 5: return 0;
            default: puts("Please enter a number from 1 to 5."); break;
        }
    }
}

void help_print(FILE *out, const char *program_name) {
    fprintf(out,
        "Personalized Learning Path and Assessment Planner - C edition\n\n"
        "Usage: %s <command> [dataset] [options]\n"
        "       (dataset defaults to the built-in sample when omitted)\n\n"
        "Commands:\n"
        "  validate [DIR]                Validate CSV data and show a summary\n"
        "  plan [DIR] [options]          Build a study/activity plan and simulate recall\n"
        "  simulate [DIR] [options]      Same planning pipeline, with simulation-focused output\n"
        "  compare [DIR] [--matrix]      Compare baseline and greedy strategies\n"
        "  matrix [DIR] [--all]          Show the Eisenhower matrix\n"
        "  algorithms                    Explain algorithms implemented in this C edition\n"
        "  demo                          Run the planner on the built-in sample\n"
        "  menu                          Open the text-based interactive menu\n"
        "  help                          Show this help\n\n"
        "Plan options:\n"
        "  --strategy greedy|baseline    Selection strategy (default: greedy)\n"
        "  --matrix                      Apply Eisenhower prioritisation during selection\n"
        "  --missed 2,3                  Simulate additional missed study days\n"
        "  --csv FILE                    Export scheduled sessions as CSV\n"
        "  --json FILE                   Export plan and simulation summary as JSON\n"
        "  --lines N                     Maximum session rows printed (default: 40)\n\n"
        "Built-in sample is available without external files.\n"
        "Optional CSV folders: data/small, data/week, data/sample, data/adversarial\n"
        "One unit = 30 minutes. Dataset paths are relative to the current working directory.\n",
        program_name);
}

int main(int argc, char **argv) {
    const char *command;
    if (argc < 2) return command_menu();
    command = argv[1];
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
        help_print(stdout, argv[0]); return 0;
    }
    if (strcmp(command, "validate") == 0) {
        const char *directory = first_directory(argc, argv);
    if (!directory) directory = "builtin";
        return command_validate(directory);
    }
    if (strcmp(command, "plan") == 0) return command_plan(argc, argv, 0);
    if (strcmp(command, "simulate") == 0) return command_plan(argc, argv, 1);
    if (strcmp(command, "compare") == 0) return command_compare(argc, argv);
    if (strcmp(command, "matrix") == 0) {
        const char *directory = first_directory(argc, argv);
    if (!directory) directory = "builtin";
        Dataset ds;
        if (!directory) { fprintf(stderr, "error: missing dataset directory\n"); return 1; }
        if (!load_and_validate(directory, &ds)) return 1;
        matrix_print(&ds, stdout, has_flag(argc, argv, "--all"));
        dataset_free(&ds); return 0;
    }
    if (strcmp(command, "algorithms") == 0) { command_algorithms(); return 0; }
    if (strcmp(command, "demo") == 0) {
        char *args[] = {argv[0], "plan", "builtin", "--matrix"};
        return command_plan(4, args, 0);
    }
    if (strcmp(command, "menu") == 0) return command_menu();
    fprintf(stderr, "error: unknown command '%s' (run '%s help')\n", command, argv[0]);
    return 1;
}

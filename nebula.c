#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
//NebulaBrute 900-2200 Req/s
#define MAX_URL_LENGTH 4096
#define MAX_WORD_LENGTH 512
#define MAX_THREADS 400
#define CONNECTION_TIMEOUT 1L
#define MAX_RETRIES 20

volatile sig_atomic_t stop_flag = 0;

void handle_signal(int sig) {
    stop_flag = 1;
    printf("\n\nSTOP....\n");
}

typedef struct {
    int value;
    pthread_mutex_t mutex;
} atomic_int;

void atomic_init(atomic_int *atomic) {
    atomic->value = 0;
    pthread_mutex_init(&atomic->mutex, NULL);
}

int atomic_load(atomic_int *atomic) {
    pthread_mutex_lock(&atomic->mutex);
    int value = atomic->value;
    pthread_mutex_unlock(&atomic->mutex);
    return value;
}

void atomic_store(atomic_int *atomic, int value) {
    pthread_mutex_lock(&atomic->mutex);
    atomic->value = value;
    pthread_mutex_unlock(&atomic->mutex);
}

int atomic_fetch_add(atomic_int *atomic, int add) {
    pthread_mutex_lock(&atomic->mutex);
    int old_value = atomic->value;
    atomic->value += add;
    pthread_mutex_unlock(&atomic->mutex);
    return old_value;
}

typedef struct {
    char *url;
    long status_code;
    long response_size;
    double total_time;
} Result;

typedef struct {
    char base_url[MAX_URL_LENGTH];
    char **wordlist;
    int wordlist_size;
    atomic_int *current_index;
    FILE *output_file;
    FILE *forbidden_file;
    pthread_mutex_t file_mutex;
    int verbose;
    int show_all;
    int *stats;
    char **extensions;
    int ext_count;
} ThreadData;

atomic_int found_count;
atomic_int total_requests;
time_t program_start;

typedef struct {
    char **pointers;
    int count;
    int capacity;
} MemoryPool;

MemoryPool* create_memory_pool(int capacity) {
    MemoryPool *pool = malloc(sizeof(MemoryPool));
    pool->pointers = malloc(capacity * sizeof(char*));
    pool->count = 0;
    pool->capacity = capacity;
    return pool;
}

void pool_add(MemoryPool *pool, char *ptr) {
    if (pool->count < pool->capacity) {
        pool->pointers[pool->count++] = ptr;
    }
}

void free_memory_pool(MemoryPool *pool) {
    for (int i = 0; i < pool->count; i++) {
        free(pool->pointers[i]);
    }
    free(pool->pointers);
    free(pool);
}

char* url_encode(const char *str) {
    if (str == NULL) return NULL;
    
    const char *hex = "0123456789ABCDEF";
    size_t len = strlen(str);
    char *encoded = malloc(len * 3 + 1);
    char *ptr = encoded;
    
    for (size_t i = 0; i < len; i++) {
        unsigned char c = str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *ptr++ = c;
        } else {
            *ptr++ = '%';
            *ptr++ = hex[c >> 4];
            *ptr++ = hex[c & 15];
        }
    }
    *ptr = '\0';
    return encoded;
}

size_t null_write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    return size * nmemb;
}

CURL* init_curl_handle() {
    CURL *curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, CONNECTION_TIMEOUT);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECTION_TIMEOUT);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, null_write_callback);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_FASTOPEN, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    }
    return curl;
}

int is_success_status(long status_code) {
    return (status_code == 200 || status_code == 201 || status_code == 202 || 
            status_code == 204 || status_code == 301 || status_code == 302 || 
            status_code == 307 || status_code == 308);
}

int is_forbidden_status(long status_code) {
    return (status_code == 403);
}

Result check_url_fast(CURL *curl, const char *url) {
    Result result;
    result.url = strdup(url);
    result.status_code = 0;
    result.response_size = 0;
    result.total_time = 0;

    for (int retry = 0; retry <= MAX_RETRIES; retry++) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            result.status_code = response_code;
            
            curl_off_t size;
            double time_taken;
            curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &size);
            curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &time_taken);
            
            result.response_size = (long)size;
            result.total_time = time_taken;
            break;
        } else if (retry == MAX_RETRIES) {
            result.status_code = -1;
        }
        
        usleep(100000);
    }
    
    return result;
}

void show_progress(ThreadData *data) {
    time_t now = time(NULL);
    int elapsed = now - program_start;
    int processed = atomic_load(&total_requests);
    int found = atomic_load(&found_count);
    int percent = (processed * 100) / data->wordlist_size;
    int req_per_sec = elapsed > 0 ? processed / elapsed : 0;
    
    printf("\rTime %ds | Statistics %d/%d (%d%%) | %d Found |  %d Req/s", 
           elapsed, processed, data->wordlist_size, percent, found, req_per_sec);
    fflush(stdout);
}

void* fast_worker_thread(void *arg) {
    ThreadData *data = (ThreadData*)arg;
    CURL *curl = init_curl_handle();
    MemoryPool *pool = create_memory_pool(1000);
    
    if (!curl) {
        pthread_exit(NULL);
    }
    
    while (!stop_flag) {
        int index = atomic_fetch_add(data->current_index, 1);
        if (index >= data->wordlist_size) break;
        
        atomic_fetch_add(&total_requests, 1);
        
        char *word_encoded = url_encode(data->wordlist[index]);
        char target_url[MAX_URL_LENGTH];
        
        if (strstr(data->base_url, "NBL")) {
            snprintf(target_url, sizeof(target_url), "%s", data->base_url);
            char *nbl_pos = strstr(target_url, "NBL");
            if (nbl_pos) {
                memmove(nbl_pos + strlen(word_encoded), nbl_pos + 3, strlen(nbl_pos + 3) + 1);
                memcpy(nbl_pos, word_encoded, strlen(word_encoded));
            }
        } else {
            snprintf(target_url, sizeof(target_url), "%s%s", data->base_url, word_encoded);
        }
        
        free(word_encoded);
        
        Result result = check_url_fast(curl, target_url);
        pool_add(pool, result.url);
        
        if (is_success_status(result.status_code)) {
            atomic_fetch_add(&found_count, 1);
            
            if (data->verbose) {
                printf("\n[%ld] %s (Size: %ld, Time: %.3fs)", 
                       result.status_code, result.url, result.response_size, result.total_time);
            } else {
                printf("\n[%ld] %s", result.status_code, result.url);
            }
            
            if (data->output_file) {
                pthread_mutex_lock(&data->file_mutex);
                fprintf(data->output_file, "%s\n", result.url);
                fflush(data->output_file);
                pthread_mutex_unlock(&data->file_mutex);
            }
        }
        else if (is_forbidden_status(result.status_code)) {
            if (data->forbidden_file) {
                pthread_mutex_lock(&data->file_mutex);
                fprintf(data->forbidden_file, "%s\n", result.url);
                fflush(data->forbidden_file);
                pthread_mutex_unlock(&data->file_mutex);
            }
            
            if (data->show_all) {
                if (data->verbose) {
                    printf("\n[%ld] %s (Size: %ld, Time: %.3fs)", 
                           result.status_code, result.url, result.response_size, result.total_time);
                } else {
                    printf("\n[%ld] %s", result.status_code, result.url);
                }
            }
        }
        else if (data->show_all) {
            if (data->verbose) {
                printf("\n[%ld] %s (Size: %ld, Time: %.3fs)", 
                       result.status_code, result.url, result.response_size, result.total_time);
            } else {
                printf("\n[%ld] %s", result.status_code, result.url);
            }
        }
        
        free(result.url);
        
        if (index % 100 == 0) {
            show_progress(data);
        }
    }
    
    curl_easy_cleanup(curl);
    free_memory_pool(pool);
    pthread_exit(NULL);
}

char** read_wordlist_fast(const char *filename, int *count) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Failed To Open WordList (Possibly Not Found)\n");
        return NULL;
    }
    
    int capacity = 10000;
    char **wordlist = malloc(capacity * sizeof(char*));
    
    char buffer[MAX_WORD_LENGTH];
    int size = 0;
    
    while (fgets(buffer, sizeof(buffer), file)) {
        buffer[strcspn(buffer, "\r\n")] = 0;
        
        if (strlen(buffer) == 0 || buffer[0] == '#') continue;
        
        if (size >= capacity) {
            capacity *= 2;
            wordlist = realloc(wordlist, capacity * sizeof(char*));
        }
        
        wordlist[size] = malloc(strlen(buffer) + 1);
        strcpy(wordlist[size], buffer);
        size++;
    }
    
    fclose(file);
    *count = size;
    
    if (size == 0) {
        printf("Wordlist Empty Or failed to read!\n");
        free(wordlist);
        return NULL;
    }
    
    printf("Wordlist loaded: %d words\n", size);
    
    return wordlist;
}

void free_wordlist(char **wordlist, int count) {
    for (int i = 0; i < count; i++) {
        free(wordlist[i]);
    }
    free(wordlist);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("NBL Scanner - High Performance Directory Brute Forcer\n");
        printf("Usage: %s -u <URL> -w <wordlist> [OPTIONS]\n\n", argv[0]);
        printf("OPTIONS:\n");
        printf("  -t <threads>    Thread count (default: 20, max: 100)\n");
        printf("  -o <output>     Output file for working URLs\n");
        printf("  -f <forbidden>  Output file for 403 Forbidden URLs\n");
        printf("  -a              Show all results\n");
        printf("  -v              Verbose mode\n");
        printf("  -e <extensions> File extensions\n\n");
        printf("EXAMPLE:\n");
        printf("  %s -u http://site.com/NBL -w wordlist.txt -t 50 -o working.txt -f forbidden.txt\n", argv[0]);
        return 1;
    }
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    char url[MAX_URL_LENGTH] = "";
    char wordlist_file[MAX_WORD_LENGTH] = "";
    char output_file[MAX_WORD_LENGTH] = "";
    char forbidden_file[MAX_WORD_LENGTH] = "";
    char extensions[256] = "";
    int thread_count = 20;
    int verbose = 0;
    int show_all = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            strncpy(url, argv[++i], sizeof(url) - 1);
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            strncpy(wordlist_file, argv[++i], sizeof(wordlist_file) - 1);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            thread_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            strncpy(output_file, argv[++i], sizeof(output_file) - 1);
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            strncpy(forbidden_file, argv[++i], sizeof(forbidden_file) - 1);
        } else if (strcmp(argv[i], "-a") == 0) {
            show_all = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            strncpy(extensions, argv[++i], sizeof(extensions) - 1);
        }
    }
    
    if (strlen(url) == 0 || strlen(wordlist_file) == 0) {
        printf("URL and Wordlist Required\n");
        return 1;
    }
    
    if (thread_count > MAX_THREADS) thread_count = MAX_THREADS;
    if (thread_count < 1) thread_count = 1;
    
    printf("Target: %s\n", url);
    printf("Threads: %d\n", thread_count);
    if (strlen(output_file) > 0) {
        printf("Output: %s\n", output_file);
    }
    if (strlen(forbidden_file) > 0) {
        printf("Forbidden: %s\n", forbidden_file);
    }
    
    int wordlist_size;
    char **wordlist = read_wordlist_fast(wordlist_file, &wordlist_size);
    if (!wordlist || wordlist_size == 0) {
        return 1;
    }
    
    atomic_init(&found_count);
    atomic_init(&total_requests);
    
    curl_global_init(CURL_GLOBAL_ALL);
    
    FILE *output_fp = NULL;
    if (strlen(output_file) > 0) {
        output_fp = fopen(output_file, "w");
        if (!output_fp) {
            printf("Failed to create output file: %s\n", output_file);
        }
    }
    
    FILE *forbidden_fp = NULL;
    if (strlen(forbidden_file) > 0) {
        forbidden_fp = fopen(forbidden_file, "w");
        if (forbidden_fp) {
            fprintf(forbidden_fp, "#403 Forbiddens (These Files/Directories Might Not Exist)\n");
        } else {
            printf("Failed to create forbidden file: %s\n", forbidden_file);
        }
    }
    
    pthread_t threads[thread_count];
    ThreadData thread_data;
    atomic_int current_index;
    atomic_init(&current_index);
    pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    strncpy(thread_data.base_url, url, sizeof(thread_data.base_url) - 1);
    thread_data.wordlist = wordlist;
    thread_data.wordlist_size = wordlist_size;
    thread_data.current_index = &current_index;
    thread_data.output_file = output_fp;
    thread_data.forbidden_file = forbidden_fp;
    thread_data.file_mutex = file_mutex;
    thread_data.verbose = verbose;
    thread_data.show_all = show_all;
    
    program_start = time(NULL);
    printf("\nBruteForce  started...\n");
    
    for (int i = 0; i < thread_count; i++) {
        pthread_create(&threads[i], NULL, fast_worker_thread, &thread_data);
    }
    
    while (atomic_load(&current_index) < wordlist_size && !stop_flag) {
        show_progress(&thread_data);
        sleep(1);
    }
    
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }
    
    time_t end_time = time(NULL);
    int total_time = end_time - program_start;
    int found = atomic_load(&found_count);
    int total_processed = atomic_load(&total_requests);
    
    printf("\n\nScan completed!\n");
    printf("Statistics:\n");
    printf("   Total time: %d seconds\n", total_time);
    printf("   Processed requests: %d/%d\n", total_processed, wordlist_size);
    printf("   Found working URLs: %d\n", found);
    printf("   Performance: %d req/s\n", total_time > 0 ? total_processed / total_time : 0);
    
    if (strlen(output_file) > 0 && found > 0) {
        printf("   Saved URLs: %s\n", output_file);
    }
    if (strlen(forbidden_file) > 0) {
        printf("   Forbidden URLs: %s\n", forbidden_file);
    }
    
    if (output_fp) fclose(output_fp);
    if (forbidden_fp) fclose(forbidden_fp);
    free_wordlist(wordlist, wordlist_size);
    curl_global_cleanup();
    
    return 0;
}

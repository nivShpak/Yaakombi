#include <iostream>
#include <fstream>
#include <bitset>
#include <chrono>
#include <unordered_map>
#include <pthread.h>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <math.h>

#include "vectors.h"
#include "settings.h"

using namespace std;
using namespace std::chrono;

extern uint64_t skippedVectors;
extern uint64_t skippedBallsCalculations;
extern uint64_t executedBallsCalculations;
int prints_counter;

struct max_vector {
    char s_vector[VECTORS_LENGTH + 1];
    int ball_size;
    int mask;
    unordered_map<string, int> *vectors_sizes;
};

int longestRun(string s) {
    int max_run = 0;
    int current_run=0;
    int i = 0;
    char curr_char = s[0];
    char next_char;
    while(i < s.size()) {
        next_char = s[i];
        if (next_char == curr_char)
            current_run++;
        else
            current_run = 1;
        if (current_run > max_run)
            max_run = current_run;
        i++;
        curr_char = next_char;
    }
    return max_run;
}

/*
 * We assume that the vector containing alternating runs, that each run is of length
 * of sqrt(n) will have a relatively large 2-indel ball size.
 */
string createStartVector(int vectorSize) {
    string s = "";
    int j = 0;
    int i = round(sqrt(vectorSize));
    int k = 0;
    string prevString = "1";
    while (j < vectorSize) {
        if (j > 0)
            k = 1;
        for (; k < i && j < vectorSize; k++, j++) {
            if (prevString == "1") {
                s.append("0");
                prevString = "0";
            } else {
                s.append("1");
                prevString = "1";
            }
        }
        if (j < vectorSize) {
            s.append(prevString);
            j++;
        }
    }
    return s;
}

bool hasRunLongerThan(string s, int x) {
    if (x <= 0)
        return false;

    int current_run=0;
    int i = 0;
    char curr_char = s[0];
    char next_char;
    while(i < s.size()) {
        next_char = s[i];
        if (next_char == curr_char)
            current_run++;
        else
            current_run = 1;
        if (current_run > x)
            return true;
        i++;
        curr_char = next_char;
    }
    return false;
}

void printAndExportHistogram(int max, unordered_map<string, int> *vectors_sizes, int isPrint, int isExport) {
    // create the histogram - it's not a full one if we check only the at most 2-length runs
    if (!isPrint && !isExport)
        return;

    int i = 0;
    int hist[(max / HISTOGRAM_BUCKET_SIZE) + 1];
    ofstream histFile;
    uint64_t totalBallsSum = 0, totalVectors = 0;

    if (isExport) {
        histFile.open(HISTOGRAM_FILE_NAME);
        if (MAX_RUN_LENGTH > 0)
            histFile << "ball_size_bucket,num_vectors(" << MAX_RUN_LENGTH << "_max_run_length_only)" << endl;
        else
            histFile << "ball_size_bucket,num_vectors" << endl;
    }
    if (isPrint) {
        cout << endl;
        if (MAX_RUN_LENGTH > 0)
            cout << "ball_size_bucket,num_vectors(" << MAX_RUN_LENGTH << "_max_run_length_only)" << endl;
        else
            cout << "ball_size_bucket,num_vectors" << endl;
    }

    for (i = 0; i <= max/HISTOGRAM_BUCKET_SIZE; i++) {
        hist[i] = 0;
    }
    for (auto iter = vectors_sizes->begin(); iter != vectors_sizes->end(); ++iter) {
        auto cur = iter->second;
        // we checked only for vectors that started with '0', so need to duplicate the numbers
        hist[cur/HISTOGRAM_BUCKET_SIZE]+=2;
        totalBallsSum += cur;
        totalVectors++;
    }

    for (i = 0; i <= max/HISTOGRAM_BUCKET_SIZE; i++) {
        if (hist[i] > 0) {
            if (isPrint)
                cout << i*HISTOGRAM_BUCKET_SIZE << "," << hist[i] << endl;
            if (isExport)
                histFile << i*HISTOGRAM_BUCKET_SIZE << "," << hist[i] << endl;
            // calculate the sum to print the avg ball size
        }
    }
    if (isExport) {
        histFile.close();
        cout << "histogram file created at: " << realpath(HISTOGRAM_FILE_NAME, nullptr) << endl;
    }
    cout << "avg ball size is: " << totalBallsSum / totalVectors << " (from " << totalVectors*2 << " vectors)" << endl;
}

void *splitCheck(void *max_vector_p) {
    auto start = steady_clock::now();
    auto middle = steady_clock::now();
    uint64_t total_vectors = 1;
    total_vectors <<= VECTORS_LENGTH;
    uint64_t vectors_calculated = 0;
    int64_t tmp_size = 0;
    struct max_vector *max_vector = ((struct max_vector *)max_vector_p);
    unordered_map<string, int> *vectors_sizes = max_vector->vectors_sizes;
    for (uint64_t i = max_vector->mask; i < total_vectors; i += NUM_THREADS) {
        string s = bitset<VECTORS_LENGTH>(i).to_string();

        if (s[0] == '1') {
            // it's enough to check ony vectors starting with '0'
            break;
        }
        
	if (VERBOSITY >= 2 && VECTORS_LENGTH > 15) {
            // 13 is a magic number so not too much is getting printed
            if ((i > 0) && (prints_counter % NUM_THREADS == max_vector->mask) && ((i/NUM_THREADS) % (1<<(VECTORS_LENGTH - 13))) == 0) {
                prints_counter++;
                cout << duration_cast<seconds>(steady_clock::now() - start).count() << "s - thread " << max_vector->mask << ": checked "
                     << i / NUM_THREADS << " of " << total_vectors / NUM_THREADS / 2 << " (" << round((i*200.0) / (total_vectors)) << "%)" << endl;
            }
        }
        
	if ((!IS_HISTOGRAM) && hasRunLongerThan(s, MAX_RUN_LENGTH)) {
            skippedVectors++;
            continue;
        }

        vectors v(s);
        tmp_size = v.twoBallSize();
        vectors_calculated++;

        if (IS_HISTOGRAM)
            vectors_sizes->insert({s, tmp_size});

        if (tmp_size >= max_vector->ball_size) {
            if (!IS_HISTOGRAM)
                vectors_sizes->insert({s, tmp_size});
            max_vector->ball_size = tmp_size;
            strncpy(max_vector->s_vector, v.get_vector().c_str(), VECTORS_LENGTH + 1);
            if (VERBOSITY >= 3) {
                middle = steady_clock::now();
                cout << duration_cast<seconds>(middle - start).count() << "s - thread " << max_vector->mask
                     << ": new max_vector: " << max_vector->s_vector << " ball size: " << tmp_size << endl;
            }
        }
    }

    if (VERBOSITY >= 1) {
        cout << "thread " << max_vector->mask << " calculated " << vectors_calculated << " vectors in "
             << duration_cast<seconds>(steady_clock::now() - start).count() << "s"
             << " max_vector is: " << max_vector->s_vector << " ball size: " << max_vector->ball_size << endl;
    }
    return nullptr;
}

void initiateMaxVector(struct max_vector &v, int mask = -1, const char *vector = nullptr) {
    if (vector)
        strncpy(v.s_vector, vector, VECTORS_LENGTH + 1);
    else
        strncpy(v.s_vector, "X", VECTORS_LENGTH + 1);
    v.ball_size = 0;
    v.vectors_sizes = new unordered_map<string, int>;
    v.mask = mask;
}

int main() {
    struct max_vector max_vector;
    unordered_map<string, int> all_vectors_sizes;
    initiateMaxVector(max_vector, -1, createStartVector(VECTORS_LENGTH).c_str());
    // initiate the arguments for the threads
    struct max_vector thread_data[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        initiateMaxVector(thread_data[i], i);
    }
    pthread_t ptid[NUM_THREADS];

    if (!IS_HISTOGRAM) {
        vectors v1(max_vector.s_vector);
        max_vector.ball_size = v1.twoBallSize();
        all_vectors_sizes.insert({v1.get_vector(), max_vector.ball_size});
    }

    // run the balls calculation in parallel
    for(int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&ptid[i], nullptr, &splitCheck, (void*)&thread_data[i]);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(ptid[i], nullptr);
    }

    // get the max vector of all the calculations
    for(int i = 0; i < NUM_THREADS; i++) {
        if (thread_data[i].ball_size > max_vector.ball_size) {
            max_vector.ball_size = thread_data[i].ball_size;
            strncpy(max_vector.s_vector, thread_data[i].s_vector, VECTORS_LENGTH + 1);
        }
    }

    for (int i=0; i<NUM_THREADS; i++) {
        for (auto it = thread_data[i].vectors_sizes->begin(); it != thread_data[i].vectors_sizes->end(); ++it) {
            all_vectors_sizes.insert({it->first, it->second});
        }
        thread_data[i].vectors_sizes->clear();
    }
    if (IS_HISTOGRAM) {
        printAndExportHistogram(max_vector.ball_size, &all_vectors_sizes, PRINT_HISTOGRAM, EXPORT_HISTOGRAM);
    }

    cout << endl;
    if (VERBOSITY >= 1) {
        uint64_t total_vectors = 1;
        total_vectors <<= (VECTORS_LENGTH-1);
        cout << "Performance info:" << endl;
        cout << "\t" << "avoided twoBallSize() calculation on " << skippedVectors << " vectors ("
        << round(skippedVectors*100.0 / total_vectors) << "%)." << endl;
        cout << "\tExecuted " << executedBallsCalculations
        << " calls to calcTwoInsertions(), while skipping "  << skippedBallsCalculations << " calls ("
        << round(skippedBallsCalculations*100.0 / (skippedBallsCalculations + executedBallsCalculations)) << "% save)."  << endl << endl;
    }
    // print the max ball size, and all it's vectors. Create the export file as well
    cout << "for n=" << VECTORS_LENGTH << " max indel-2 ball size is " << max_vector.ball_size
         << ". max vectors are:" << endl;
    ofstream outputFile;
    if (OUTPUT_MAX_VECTORS > 0) {
        outputFile.open(MAX_VECTORS_FILE);
        outputFile << "n" << "," << "vector" << "," << "ball_size" << endl;
    }
        for (auto iter = all_vectors_sizes.begin(); iter != all_vectors_sizes.end(); ++iter) {
        if (iter->second == max_vector.ball_size) {
            cout << "\t" << iter->first << endl;
            if (OUTPUT_MAX_VECTORS > 0) {
                outputFile << VECTORS_LENGTH << "," << iter->first << "," << iter->second << endl;
            }
        }
    }
    cout << endl;

    // cleanup
    for (int i = 0; i < NUM_THREADS; i++) {
        delete thread_data[i].vectors_sizes;
    }
    delete max_vector.vectors_sizes;

    return 0;
}

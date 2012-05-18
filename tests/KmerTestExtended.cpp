#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <ctime>
#include <map>

#include "../Kmer.hpp"

using namespace std;

// Purpose: Test the kmer operators (< and ==)
//          Verify that the twin method gives the right twin
int main(int argc, char *argv[]) {
    if (argc < 3) {
        cout << "usage: KmerTestExtended <k> <n>\n Where k is kmer size and 10^n random strings will be tested" << endl;
        return 1;
    }

    // Read k from argument and set kmer size
    unsigned int k = atoi(argv[1]), n = atoi(argv[2]), limit = 1, index, j;

    for (j=0; j<n; j++) 
        limit *= 10;

    Kmer::set_k(k);

    // Allocate strings to use
    char *s = (char *) malloc((k + 1) * sizeof(char));
    char *last = (char *) malloc((k + 1) * sizeof(char));
    char *twin = (char *) malloc((k + 1) * sizeof(char));
    char letters[] = {'A', 'C', 'G', 'T'};
    map<int, int> baseKey;
	baseKey['A'] = 0; baseKey['C'] = 1; baseKey['G'] = 2; baseKey['T'] = 3;

    // Initialize the strings
    for(j=0; j<k; j++)
        s[j] = last[j] = 'A';
    s[k] = last[k] = '\0';

    Kmer K, Kp, Ktwin;

    // Offset the random feeder with time
    srand(time(NULL));

    for (index = 0; index < limit; index++) {
        strcpy(last,s);

        // Put random A,C,G,T characters into s
        for(j=0; j<k; j++)
            s[j] = letters[rand() & 3];
        s[k] = '\0';

        K = Kmer(s);
        Ktwin = K.twin();
        Ktwin.toString(twin);
        for(j=0; j<k; j++) {
			if (twin[k-1-j] != letters[3-baseKey[s[j]]]) {
				cout << "Incorrect twin: " << twin << " from: " << s << endl;
				return 1;
			}
        }


        if (index > 0) {
            Kp = Kmer(last);
            
            if (Kp < K) {
                if (strcmp(last, s) >= 0) {
                    cout << "Kmer with string: " << last << " is less than kmer with string: " << s << endl;
                    return 1;
                }
            } else {
                if (strcmp(last, s) < 0) {
                    cout << "Kmer with string: " << last << " is greater or equal than kmer with string: " << s << endl;
                    return 1;
                }
            }
            
            if (Kp == K) {
                if (strcmp(last, s) != 0) {
                    cout << "Kmer with string: " << last << " is equal to kmer with string: " << s << endl;
                    return 1;
                }
            }
        }
    }
    cout << "All tests completed successfully" << endl;
    return 0;
}

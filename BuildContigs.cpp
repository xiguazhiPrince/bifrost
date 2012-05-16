#include <iostream>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <cstring>
#include <utility>

#include <sstream>
#include <vector>
#include <string>

#include <stdint.h>
#include <sys/stat.h>
#include <functional>

#include <getopt.h>

#include "Common.hpp"

#include "HashTables.hpp"
#include "fastq.hpp"
#include "Kmer.hpp"
#include "BloomFilter.hpp"
#include "CompressedSequence.hpp"
#include "KmerMapper.hpp"
#include "Contig.hpp"


pair<Kmer, size_t> find_contig_forward(BloomFilter &bf, Kmer km, string* s);

struct BuildContigs_ProgramOptions {
  size_t k;
  size_t contig_size; // not configurable
  string input;
  string output;
  bool verbose;
  vector<string> files;
  BuildContigs_ProgramOptions() : k(0), verbose(false) , contig_size(1000000) {}
};

void BuildContigs_PrintUsage() {
  cerr << "BFGraph " << BFC_VERSION << endl << endl;
  cerr << "Filters errors in fastq or fasta files and saves results" << endl << endl;
  cerr << "Usage: BFGraph contigs [options] ... FASTQ files";
  cerr << endl << endl <<
    "-k, --kmer-size=INT             Size of k-mers, at most " << (int) (Kmer::MAX_K-1)<< endl << 
    "-i, --input=STRING            Filtered reads" << endl <<
    "-o, --output=STRING             Filename for output" << endl <<
    "    --verbose                   Print lots of messages during run" << endl << endl
    ;
}




void BuildContigs_ParseOptions(int argc, char **argv, BuildContigs_ProgramOptions &opt) {
  int verbose_flag = 0;
  const char* opt_string = "k:o:i:";
  static struct option long_options[] =
  {
    {"verbose", no_argument,  &verbose_flag, 1},
    {"kmer-size", required_argument, 0, 'k'},
    {"input", required_argument, 0, 'i'},
    {"output", required_argument, 0, 'o'},
    {0,0,0,0}
  };

  int option_index = 0;
  int c;
  stringstream ss;
  stringstream ss2;
  while (true) {
    c = getopt_long(argc,argv,opt_string, long_options, &option_index);
    //cout << "debug: c="<<c<<", optarg="<<optarg << endl;
    if (c == -1) {
      break;
    }

    switch (c) {
    case 0: 
      break;
    case 'k': 
      opt.k = atoi(optarg); 
      break;
    case 'o': 
      opt.output = optarg;
      break;
    case 'i':
      opt.input = optarg;
      break;
    default: break;
    }
  }

  // all other arguments are fast[a/q] files to be read
  for (int i = optind; i < argc; i++) {
    opt.files.push_back(argv[i]);
  }
  
  if (verbose_flag) {
    opt.verbose = true;
  }
}


bool BuildContigs_CheckOptions(BuildContigs_ProgramOptions &opt) {
  bool ret = true;

  if (opt.k <= 0 || opt.k >= MAX_KMER_SIZE) {
    cerr << "Error, invalid value for kmer-size: " << opt.k << endl;
    cerr << "Values must be between 1 and " << (MAX_KMER_SIZE-1) << endl;
    ret = false;
  }

  
  if (opt.input.empty()) {
    cerr << "Input file missing" << endl;
  } else {
    struct stat inFileInfo;
    if (stat(opt.input.c_str(), &inFileInfo) != 0) {
      cerr << "Error input file not found " << opt.input << endl;
      ret = false;
    }
  }

  if (opt.files.size() == 0) {
    cerr << "Need to specify files for input" << endl;
    ret = false;
  } else {
    struct stat stFileInfo;
    vector<string>::const_iterator it;
    int intStat;
    for(it = opt.files.begin(); it != opt.files.end(); ++it) {
      intStat = stat(it->c_str(), &stFileInfo);
      if (intStat != 0) {
	cerr << "Error: file not found, " << *it << endl;
	ret = false;
      }
    }
  }
  
 

  //TODO: check if we have permission to write to outputfile
  
  return ret;

}

void BuildContigs_PrintSummary(const BuildContigs_ProgramOptions &opt) {
  cerr << "Kmer size" << opt.k << endl
       << "Reading input file " << opt.input << endl
       << "Writing to output " << opt.output << endl
       << "input files: " << endl;
  vector<string>::const_iterator it;
  for (it = opt.files.begin(); it != opt.files.end(); ++it) {
    cerr << "  " << *it << endl;
  }
  
}

void BuildContigs_Normal(const BuildContigs_ProgramOptions &opt) {

  BloomFilter bf;
  FILE* f = fopen(opt.input.c_str(), "rb");
  if (f == NULL) {
    cerr << "Error, could not open file " << opt.input << endl;
    exit(1);
  } 
  if (!bf.ReadBloomFilter(f)) {
    cerr << "Error reading bloom filter from file " << opt.input << endl;
    fclose(f);
    f = NULL;
    exit(1);
  } else {
    fclose(f);
    f = NULL;
  }

  KmerMapper mapper(opt.contig_size);

  size_t k  = Kmer::k;
  char name[8196],s[8196];
  size_t name_len,len;

  uint64_t n_read = 0;
  uint64_t num_kmers = 0;  
  uint64_t num_ins = 0;
 
  // loops over all files
  FastqFile FQ(opt.files);
  
  vector<Kmer> kmers;
  vector<Kmer> reps;

  //
  cerr << "starting real work" << endl;

  while (FQ.read_next(name, &name_len, s, &len, NULL, NULL) >= 0) {
    // discard N's
    n_read++;
    kmers.clear();
    reps.clear();

    Kmer km(s);
    kmers.push_back(km);
    for (size_t i = 0; i <= len-k; i++) {
      km = km.forwardBase(s[i+k-1]);
      kmers.push_back(km);
      reps.push_back(km.rep());
    }
    size_t i = 0;
    while (i < kmers.size()) {
      
      if (!bf.contains(reps[i])) { // kmer i is not in the graph
	i++; // jump over it
      } else {
	ContigRef cr = mapper.find(km);

	if (cr.isEmpty()) {
	  // ok, didn't find the k-mer, search for it

	  pair<Kmer, size_t> p_fw,p_bw;
	  p_fw = find_contig_forward(bf,kmers[i],NULL);
	  
	  ContigRef cr_end = mapper.find(p_fw.first);
	  if (cr_end.isEmpty()) {
	    
	    string seq, seq_fw(k,0), seq_bw(k,0);
	    // 
	    p_fw = find_contig_forward(bf,kmers[i],&seq_fw);
	    p_bw = find_contig_forward(bf,kmers[i].twin(),&seq_bw);
	    ContigRef cr_tw_end = mapper.find(p_bw.first);
	    assert(cr_tw_end.isEmpty());

	    if (p_bw.second > 1) {
	      seq.reserve(seq_bw.size() + seq_fw.size() - k);
	      // copy reverse part of seq_bw not including k
	      // TODO: refactor this to util package
	      for (int j = seq_bw.size()-k-1; j>=0; j--) {
		char c = seq_bw[j];
		char cc = 'N';
		switch (c) {
		case 'A': cc = 'T'; break;
		case 'C': cc = 'G'; break;
		case 'G': cc = 'C'; break;
		case 'T': cc = 'A'; break;
		}
		seq.push_back(cc);
	      }
	      // append seq_fw
	      seq += seq_fw;
	    } else {
	      seq = seq_fw;
	    }
	    mapper.addContig(seq);
	    cerr << seq.size() << endl;
	    ContigRef found = mapper.find(kmers[i]);
	    if (!found.isEmpty()) {
	      mapper.printContig(found.ref.idpos.id);
	    }
	  }

	  // jump over contig
	  i += p_fw.second;
	} else {
	  //cerr << "found" << endl;
	  // already found
	  // how much can we jump ahead?
	  Contig *contig = mapper.getContig(cr).ref.contig;
	  int32_t pos = cr.ref.idpos.pos;
	  int32_t len = (int32_t) contig->seq.size();
	  if (pos >= 0) {
	    // kmer i is on forward strand
	    assert(len-pos-k >= 0);
	    i += len-pos-k + 1; // jump over contig
	  } else {
	    // kmer i is on reverse strand	    
	    assert(-pos >= k-1);
	    i += 2 - (pos + k); // jump over contig
	  }
	}
      }     
    }
    
  }
  
  cerr << "Number of reads " << n_read  << ", kmers stored " << mapper.size()<< endl;
  
  /*
    outline of algorithm
    - open bloom filter file
    - create contig datastructures
    - for each read
      - generate k-mers
      - map to contig if in bf
        - if unique move to boundary, repeat with next
	 - check if we can extend current contig or branch or merge
        - if not found, create new contig

      - implement greedy strategy
   
   */
}


void BuildContigs(int argc, char** argv) {
  
  BuildContigs_ProgramOptions opt;

  BuildContigs_ParseOptions(argc,argv,opt);

  if (argc < 2) {
    BuildContigs_PrintUsage();
    exit(1);
  }
  
  if (!BuildContigs_CheckOptions(opt)) {
    BuildContigs_PrintUsage();
    exit(1);
  }
  
  // set static global k-value
  Kmer::set_k(opt.k);

  if (opt.verbose) {
    BuildContigs_PrintSummary(opt);
  }

  BuildContigs_Normal(opt);

}


// use:  (end,dist) = find_contig_forward(bf,km,s);
// pre:  
// post: km is contained in a contig c with respect to the
//       bloom filter graph bf and end is the forward endpoint (wrt km direction)
//       and is contains dist kmers until the end (including km)
//       if s is not NULL the sequence of the contig is stored in s
pair<Kmer, size_t> find_contig_forward(BloomFilter &bf, Kmer km, string* s) {
  //TODO: add string return option
  vector<char> v;
  if (s != NULL) {
    char t[Kmer::MAX_K+1];
    km.toString(t);
    for (int i = 0; i < Kmer::k; i++) {
      v.push_back(t[i]);
    }
  }
  Kmer end = km;
  
  size_t dist = 1;

  Kmer fw,bw;
  const char alpha[4] = {'A','C','G','T'};

  while (true) {
    size_t fw_count = 0;
    int j = -1;
    for (int i = 0; i < 4; i++) {
      Kmer fw_rep = end.forwardBase(alpha[i]).rep();
      if (bf.contains(fw_rep)) {
	j = i;
	fw_count++;
	if (fw_count > 1) {
	  break;
	}
      }
    }

    if (fw_count != 1) {
      break;
    }
    
    fw = end.forwardBase(alpha[j]);

    size_t bw_count = 0;
    for (int i = 0; i < 4; i++) {
      Kmer bw_rep = fw.backwardBase(alpha[i]).rep();
      if (bf.contains(bw_rep)) {
	bw_count++;
	if (bw_count > 1) {
	  break;
	}
      }
    }

    if (bw_count != 1) {
      break;
    }
    
    
    end = fw;
    dist++;
    if (s != NULL) {
      v.push_back(alpha[j]);
    }
  }

  if (s != NULL) {
    s->clear();
    s->reserve(v.size());
    s->insert(s->begin(),v.begin(),v.end());
  }
  return make_pair(end,dist);
}
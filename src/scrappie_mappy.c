#define _XOPEN_SOURCE 700
#include <math.h>
#include <stdio.h>
#include <strings.h>
#include <sys/types.h>

#include "decode.h"
#include "fast5_interface.h"
#include "networks.h"
#include "scrappie_common.h"
#include "scrappie_licence.h"
#include "scrappie_seq_helpers.h"
#include "scrappie_stdlib.h"
#include "util.h"

const size_t NCAT = 5;


// Doesn't play nice with other headers, include last
#include <argp.h>


extern const char *argp_program_version;
extern const char *argp_program_bug_address;
static char doc[] = "Scrappie squiggler";
static char args_doc[] = "fasta fast5";
static struct argp_option options[] = {
    {"model", '1', "name", 0, "Squiggle model to use: \"squiggle_r94\", \"squiggle_r10\""},
    {"backprob", 'b', "probability", 0, "Probability of backwards movement"},
    {"skippen", 'k', "float", 0, "Penalty for skipping position"},
    {"localpen", 'l', "float", 0, "Penalty for local matching"},
    {"minscore", 'm', "float", 0, "Minimum possible score for matching emission"},
    {"output", 'o', "filename", 0, "Write to file rather than stdout"},
    {"prefix", 'p', "string", 0, "Prefix to append to name of read"},
    {"rate",'r', "float", 0, "Translocation rate of read relative to standard squiggle"},
    {"segmentation", 's', "chunk:percentile", 0, "Chunk size and percentile for variance based segmentation"},
    {"speed", 2, "shape", 0, "Shape for speed distribution"},
    {"trim", 't', "start:end", 0, "Number of samples to trim, as start:end"},
    {"licence", 10, 0, 0, "Print licensing information"},
    {"license", 11, 0, OPTION_ALIAS, "Print licensing information"},
    {0}
};


struct arguments {
    enum squiggle_model_type model_type;
    float backprob;
    float skippen;
    float localpen;
    float minscore;
    float rate;
    FILE * output;
    char * prefix;
    float shape;
    int trim_start;
    int trim_end;
    int varseg_chunk;
    float varseg_thresh;

    char * fasta_file;
    char * fast5_file;
};

static struct arguments args = {
    .model_type = SCRAPPIE_SQUIGGLE_MODEL_R9_4,
    .backprob = 0.0f,
    .skippen = 5000.0f,
    .localpen = 2.0f,
    .minscore = 5.0f,
    .rate = 1.0f,
    .output = NULL,
    .prefix = "",
    .shape = 0,
    .trim_start = 200,
    .trim_end = 10,
    .varseg_chunk = 100,
    .varseg_thresh = 0.0f,

    .fasta_file = NULL,
    .fast5_file = NULL
};

static error_t parse_arg(int key, char *arg, struct argp_state *state) {
    int ret = 0;
    char * next_tok = NULL;

    switch (key) {
    case 1:
        args.model_type = get_squiggle_model(arg);
        if(SCRAPPIE_SQUIGGLE_MODEL_INVALID == args.model_type){
            errx(EXIT_FAILURE, "Invalid squiggle model name \"%s\"", arg);
        }
        break;
    case 'b':
        args.backprob = atof(arg);
        if(args.backprob < 0.0 && args.backprob >= 1.0){
            errx(EXIT_FAILURE, "Backwards probability must be in [0, 1). Got %f", args.backprob);
        }
        break;
    case 'k':
        args.skippen = atof(arg);
        break;
    case 'l':
        args.localpen = atof(arg);
        break;
    case 'm':
        args.minscore = atof(arg);
        break;
    case 'o':
        args.output = fopen(arg, "w");
        if(NULL == args.output){
            errx(EXIT_FAILURE, "Failed to open \"%s\" for output.", arg);
        }
        break;
    case 'p':
        args.prefix = arg;
        break;
    case 'r':
        args.rate = atof(arg);
        if(args.rate <= 0.0f){
            errx(EXIT_FAILURE, "Rate must be positive, got %f", args.rate);
        }
        break;
    case 's':
        args.varseg_chunk = atoi(strtok(arg, ":"));
        next_tok = strtok(NULL, ":");
        if(NULL == next_tok){
            errx(EXIT_FAILURE, "--segmentation should be of form chunk:percentile");
        }
        args.varseg_thresh = atof(next_tok) / 100.0;
        assert(args.varseg_chunk >= 0);
        assert(args.varseg_thresh > 0.0 && args.varseg_thresh < 1.0);
        break;
    case 't':
        args.trim_start = atoi(strtok(arg, ":"));
        next_tok = strtok(NULL, ":");
        if(NULL != next_tok){
            args.trim_end = atoi(next_tok);
        } else {
            args.trim_end = args.trim_start;
        }
        assert(args.trim_start >= 0);
        assert(args.trim_end >= 0);
        break;
    case 2:
        args.shape = atof(arg);
        if(args.shape < 0.0){
            errx(EXIT_FAILURE, "--speed should be positive");
        }
        break;
    case 10:
    case 11:
        ret = fputs(scrappie_licence_text, stdout);
        exit((EOF != ret) ? EXIT_SUCCESS : EXIT_FAILURE);
        break;

    case ARGP_KEY_NO_ARGS:
        argp_usage(state);
        break;

    case ARGP_KEY_ARG:
        args.fasta_file = state->argv[state->next - 1];
        if(NULL == state->argv[state->next]){
            errx(EXIT_FAILURE, "fast5 file is a required argument");
        }
        args.fast5_file = state->argv[state->next];
        state->next = state->argc;
        break;

    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { options, parse_arg, args_doc, doc };


static scrappie_matrix sequence_to_squiggle(char const * base_seq, size_t n, bool rescale, enum squiggle_model_type squiggle_model){
    RETURN_NULL_IF(NULL == base_seq, NULL);

    int * sequence = encode_bases_to_integers(base_seq, n, 1);
    RETURN_NULL_IF(NULL == sequence, NULL);

    squiggle_function_ptr squiggle_function = get_squiggle_function(squiggle_model);

    scrappie_matrix squiggle = squiggle_function(sequence, n, rescale);
    free(sequence);

    return squiggle;
}



int main_mappy(int argc, char *argv[]) {
    argp_parse(&argp, argc, argv, 0, 0, NULL);
    if(NULL == args.output){
        args.output = stdout;
    }


    //  Open sequence file
    scrappie_seq_t seq = read_sequence_from_fasta(args.fasta_file);
    if(NULL == seq.seq){
        warnx("Failed to open \"%s\" for input.\n", args.fasta_file);
        return EXIT_FAILURE;
    }

    //  Read raw signal and normalise
    raw_table rt = read_raw(args.fast5_file, true);
    rt = trim_and_segment_raw(rt, args.trim_start, args.trim_end, args.varseg_chunk, args.varseg_thresh);
    if(NULL == rt.raw){
        warnx("Failed to open \"%s\" for input and trim signal.\n", args.fasta_file);
        return EXIT_FAILURE;
    }
    medmad_normalise_array(rt.raw + rt.start, rt.end - rt.start);


    scrappie_matrix squiggle = sequence_to_squiggle(seq.seq, seq.n, false, args.model_type);
    if(NULL != squiggle){
        float score[NCAT];
        float scale = sinf(M_PI / args.shape) / (M_PI / args.shape);
        for(size_t i=0 ; i < NCAT ; i++){
            float p = (i + 1.0) / (NCAT + 1);
            // Log-logistic distribution
            float speed = scale * powf((1.0 - p) / p, -1.0 / args.shape);
            score[i] = squiggle_match_forward(rt, speed, squiggle, args.backprob, args.localpen, args.skippen, args.minscore);
        }

        float max_score = valmaxf(score, NCAT);
        float Z = expf(score[0] - max_score);
        for(size_t i=1 ; i < NCAT ; i++){
            Z += expf(score[i] - max_score);
        }

        float post_mean = 0.0;
        float post_meansqr = 0.0;
        for(size_t i=0 ; i < NCAT ; i++){
            float p = (i + 1.0) / (NCAT + 1);
            float speed = scale * powf((1.0 - p) / p, -1.0 / args.shape);
            float post = expf(score[i] - max_score) / Z;
            printf("%zu\t%f\t%f\t%f\t%f\n", i, speed, 1.0 / NCAT, post, score[i]);
            post_mean += post * speed;
            post_meansqr += post * speed * speed;
        }
        printf("#  post mean = %f  post sd = %f\n", post_mean, sqrtf(post_meansqr - post_mean * post_mean));

        squiggle = free_scrappie_matrix(squiggle);
    }


    free(seq.seq);
    free(seq.name);

    return EXIT_SUCCESS;
}

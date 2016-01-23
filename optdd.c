// parse commandline options
#include <getopt.h> // getopt_long, optind
// constants
#include <stdlib.h> // EXIT_FAILURE, EXIT_SUCCESS
// unix stuff
#include <unistd.h> // getpagesize, sysconf
// macros
#include <stddef.h> // size_t
// integer types
#include <stdint.h> // uintmax_t
// error reporting (original dd used "error.h", to allow local directory overriding)
// the gnu dd used: http://git.savannah.gnu.org/cgit/gnulib.git/tree/lib/error.h
#include <error.h> // error
// IO
#include <stdio.h> // fprintf, stderr


#define PROGRAM_NAME "dd"

// quoting makes things printable
// the gnu dd used: http://git.savannah.gnu.org/cgit/gnulib.git/tree/lib/quote.h
// #include "quote.h"
// other things
// #include "system.h"

// here static on an already global variable means its namespaced to the local file
// when static is used inside a function, it makes the variable live for the entire program duration
// its visibility is still limited to the function context though, nobody else can access it
static size_t page_size;

/* The name of the input file, or NULL for the standard input. */
static char const * input_file = NULL;

/* The name of the output file, or NULL for the standard output. */
static char const * output_file = NULL;

/* Conversions bit masks. */
// bit flags is more faster than string comparison
enum {
    C_ASCII = 01,

    C_EBCDIC = 02,
    C_IBM = 04,
    C_BLOCK = 010,
    C_UNBLOCK = 020,
    C_LCASE = 040,
    C_UCASE = 0100,
    C_SWAB = 0200,
    C_NOERROR = 0400,
    C_NOTRUNC = 01000,
    C_SYNC = 02000,

    /* Use separate input and output buffers, and combine partial
       input blocks. */
    C_TWOBUFS = 04000,

    C_NOCREAT = 010000,
    C_EXCL = 020000,
    C_FDATASYNC = 040000,
    C_FSYNC = 0100000,

    C_SPARSE = 0200000
};

/* Conversion symbols, for conv="...".  */
static struct symbol_value const conversions[] = {
    {"ascii", C_ASCII | C_UNBLOCK | C_TWOBUFS},   /* EBCDIC to ASCII. */
    {"ebcdic", C_EBCDIC | C_BLOCK | C_TWOBUFS},   /* ASCII to EBCDIC. */
    {"ibm", C_IBM | C_BLOCK | C_TWOBUFS}, /* Different ASCII to EBCDIC. */
    {"block", C_BLOCK | C_TWOBUFS},   /* Variable to fixed length records. */
    {"unblock", C_UNBLOCK | C_TWOBUFS},   /* Fixed to variable length records. */
    {"lcase", C_LCASE | C_TWOBUFS},   /* Translate upper to lower case. */
    {"ucase", C_UCASE | C_TWOBUFS},   /* Translate lower to upper case. */
    {"sparse", C_SPARSE},     /* Try to sparsely write output. */
    {"swab", C_SWAB | C_TWOBUFS}, /* Swap bytes of input. */
    {"noerror", C_NOERROR},   /* Ignore i/o errors. */
    {"nocreat", C_NOCREAT},   /* Do not create output file.  */
    {"excl", C_EXCL},     /* Fail if the output file already exists.  */
    {"notrunc", C_NOTRUNC},   /* Do not truncate output file. */
    {"sync", C_SYNC},     /* Pad input records to ibs with NULs. */
    {"fdatasync", C_FDATASYNC},   /* Synchronize output data before finishing.  */
    {"fsync", C_FSYNC},       /* Also synchronize output metadata.  */
    {"", 0}
};

// function prototypes to allow out of order usage!
void usage (int status);
static void scanargs (int argc, char * const * argv);


void 
usage (int status) {
    
    if (status != EXIT_SUCCESS) {
        fprintf (stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
        // we don't have a --help section yet!
    } else {
        exit (status);
    }

}

// C strings cannot be compared using just ==, they must be compared bytewise
// there is strcmp for bytewise comparison, but here, this function takes 
// as operand a "key=value", and a name being "if"
// so it's not full comparison, it's prefix matching
static bool operand_is (char const * operand, char const * name) {
    return operand_matches (operand, name, '=');
}

static bool operand_matches (char const * str, char const * pattern, char delim) {

    // *pattern gives us the first character
    // *pattern fails when the pointer is pointing to the NULL, which means end of array (end of C string)
    while (*pattern) {
        // means *(str++), *(pattern++)
        // post ++ is a procedure with a side effect of incrementing the pointer, while returning a reference to the old value
        // if "k" == "k", it's all good
        // then e == e
        // then y == y ...
        // but pattern stops, say `if` it stops at `f`
        // the prefix match has completed, and loop finishes
        // so false is not returned
        if (*str++ != *pattern++) {
            return false;
        }
    }
    
    // if the str contains no more characters, as in *str == NULL, then !*str flips the NULL and returns TRUE
    // that short circuits, and the entire match succeeds, this means `dd key` is valid
    // however if there is another character like `dd keyy`, and we were looking for key, then it's a FALSE
    // except where tht other character is equal to the delimiter, for example `dd key=`
    // previously scanargs made sure that `dd key=val` must be true as there must be a val
    return !*str || *str == delim;

}


// static here means local namespace, this scanargs name will not conflict with any other uses of scanargs
// in other files, also cannot be called from outside of this file, kind of a file local namespace
// char * const * argv means, argv as an array of pointers, each pointer is constant, each pointer points to a C null terminated string
// remember that argv resembles space separated strings like ["dd" "k=v" "kk=vv"]
// A C string is always `char * const`.
// alternatively `char const * const * argv` is possible too (representing a C null terminated string)
// its basically saying that it won't change the argv contents
static void 
scanargs (int argc, char * const * argv) {

    // here it's signed int, at least 16 bits
    int i;
    
    // size_t here represents array index type, and the result of sizeof, will be at least as big as the largest in-memory object in the current runtime
    // here it uses size_t not for array indexing, but for its size
    // it will be at least 16 bits
    size_t blocksize = 0;

    // uintmax_t is a type alias resolved at compile time, it is resolved to one of the native uint types
    // like uint64_t, it will be at least 64 bits
    // uintmax_t is a type representing the largest possible integer (unsigned)
    // (uintmax_t) -1 is typecasting -1, resulting in a underflow due to being unsigned, this results in the 
    // largest possible number given to the count variable
    uintmax_t count = (uintmax_t) -1;
    
    uintmax_t skip = 0;
    
    uintmax_t seek = 0;

    // optind got set by getopt_long, the getopt_long seeked past all the irrelevant parameters
    // so if you passed dd if=..., then optind == 1 as argv[0] is always program anme
    // if you passed dd -- if=..., then optind == 2, as 1 is taken up by the `--`
    for (i = optind; i < argc; i++) {

        // here we are going to scan all of the arguments, and parse the dd style arguments
        // key=value style arguments

        // take an argument out of argv, it's going to the a pointer, specifically a pointer to a constant character
        // this represents a position in the argv buffer, in which we can start parsing `key=value`
        char const * name = argv[i];

        // strchr means string-character, so looking for a character given a C string/pointer
        // so name is basically like if=/file
        // so strchr returns a pointer to exactly where = is
        // ```
        // if=/file
        //   |
        //   strchr
        // ```
        // strchr doesn't change name, name is still "k=v", instead val ends up being an incremented pointer position relative to name
        // that is, val = "=value", name = "key=value"
        char const * val = strchr (name, '=');

        // if strchr did not find a `=`, it returns NULL
        if (val == NULL) {
            // error status 0, error number 0, format string...
            // if error status was non 0 number, then error calls exit
            // if error number is non 0 number, then the message printed is different
            error (0, 0, "unrecognized operand %s", name);
            // calls usage function with failure!
            usage (EXIT_FAILURE);
        }

        // if we found "=value", increment the pointer so that val = "value"
        val++;
        
        // dd is also block buffered even when used interactively
        // but cat is line buffered when used interactively
        
        // case doesn't work with anything other than integers in C, so we have to use if after if

        if (operand_is (name, "if")) {
            
            input_file = val;
        
        } else if (operand_is (name, "of")) {
            
            output_file = val;
        
        } else if (operand_is (name, "conv")) {
            
            conversions_mask |= parse_symbols (val, conversions, false, "invalid conversion");
        
        } else if (operand_is (name, "iflag")) {
            input_flags |= parse_symbols (val, flags, false, N_("invalid input flag"));
        } else if (operand_is (name, "oflag")) {
            output_flags |= parse_symbols (val, flags, false, N_("invalid output flag"));
        } else if (operand_is (name, "status")) {
            status_level = parse_symbols (val, statuses, true, N_("invalid status level"));
        } else {

          strtol_error invalid = LONGINT_OK;
          uintmax_t n = parse_integer (val, &invalid);
          uintmax_t n_min = 0;
          uintmax_t n_max = UINTMAX_MAX;

          if (operand_is (name, "ibs"))
            {
              n_min = 1;
              n_max = MAX_BLOCKSIZE (INPUT_BLOCK_SLOP);
              input_blocksize = n;
            }
          else if (operand_is (name, "obs"))
            {
              n_min = 1;
              n_max = MAX_BLOCKSIZE (OUTPUT_BLOCK_SLOP);
              output_blocksize = n;
            }
          else if (operand_is (name, "bs"))
            {
              n_min = 1;
              n_max = MAX_BLOCKSIZE (INPUT_BLOCK_SLOP);
              blocksize = n;
            }
          else if (operand_is (name, "cbs"))
            {
              n_min = 1;
              n_max = SIZE_MAX;
              conversion_blocksize = n;
            }
          else if (operand_is (name, "skip"))
            skip = n;
          else if (operand_is (name, "seek"))
            seek = n;
          else if (operand_is (name, "count"))
            count = n;
          else
            {
              error (0, 0, _("unrecognized operand %s"),
                     quote (name));
              usage (EXIT_FAILURE);
            }

          if (n < n_min)
            invalid = LONGINT_INVALID;
          else if (n_max < n)
            invalid = LONGINT_OVERFLOW;

          if (invalid != LONGINT_OK)
            error (EXIT_FAILURE, invalid == LONGINT_OVERFLOW ? EOVERFLOW : 0,
                   "%s: %s", _("invalid number"), quote (val));
        }
    }

  if (blocksize)
    input_blocksize = output_blocksize = blocksize;
  else
    {
      /* POSIX says dd aggregates partial reads into
         output_blocksize if bs= is not specified.  */
      conversions_mask |= C_TWOBUFS;
    }

  if (input_blocksize == 0)
    input_blocksize = DEFAULT_BLOCKSIZE;
  if (output_blocksize == 0)
    output_blocksize = DEFAULT_BLOCKSIZE;
  if (conversion_blocksize == 0)
    conversions_mask &= ~(C_BLOCK | C_UNBLOCK);

  if (input_flags & (O_DSYNC | O_SYNC))
    input_flags |= O_RSYNC;

  if (output_flags & O_FULLBLOCK)
    {
      error (0, 0, "%s: %s", _("invalid output flag"), quote ("fullblock"));
      usage (EXIT_FAILURE);
    }

  if (input_flags & O_SEEK_BYTES)
    {
      error (0, 0, "%s: %s", _("invalid input flag"), quote ("seek_bytes"));
      usage (EXIT_FAILURE);
    }

  if (output_flags & (O_COUNT_BYTES | O_SKIP_BYTES))
    {
      error (0, 0, "%s: %s", _("invalid output flag"),
             quote (output_flags & O_COUNT_BYTES
                    ? "count_bytes" : "skip_bytes"));
      usage (EXIT_FAILURE);
    }

  if (input_flags & O_SKIP_BYTES && skip != 0)
    {
      skip_records = skip / input_blocksize;
      skip_bytes = skip % input_blocksize;
    }
  else if (skip != 0)
    skip_records = skip;

  if (input_flags & O_COUNT_BYTES && count != (uintmax_t) -1)
    {
      max_records = count / input_blocksize;
      max_bytes = count % input_blocksize;
    }
  else if (count != (uintmax_t) -1)
    max_records = count;

  if (output_flags & O_SEEK_BYTES && seek != 0)
    {
      seek_records = seek / output_blocksize;
      seek_bytes = seek % output_blocksize;
    }
  else if (seek != 0)
    seek_records = seek;

  /* Warn about partial reads if bs=SIZE is given and iflag=fullblock
     is not, and if counting or skipping bytes or using direct I/O.
     This helps to avoid confusion with miscounts, and to avoid issues
     with direct I/O on GNU/Linux.  */
  warn_partial_read =
    (! (conversions_mask & C_TWOBUFS) && ! (input_flags & O_FULLBLOCK)
     && (skip_records
         || (0 < max_records && max_records < (uintmax_t) -1)
         || (input_flags | output_flags) & O_DIRECT));

  iread_fnc = ((input_flags & O_FULLBLOCK)
               ? iread_fullblock
               : iread);
  input_flags &= ~O_FULLBLOCK;

  if (multiple_bits_set (conversions_mask & (C_ASCII | C_EBCDIC | C_IBM)))
    error (EXIT_FAILURE, 0, _("cannot combine any two of {ascii,ebcdic,ibm}"));
  if (multiple_bits_set (conversions_mask & (C_BLOCK | C_UNBLOCK)))
    error (EXIT_FAILURE, 0, _("cannot combine block and unblock"));
  if (multiple_bits_set (conversions_mask & (C_LCASE | C_UCASE)))
    error (EXIT_FAILURE, 0, _("cannot combine lcase and ucase"));
  if (multiple_bits_set (conversions_mask & (C_EXCL | C_NOCREAT)))
    error (EXIT_FAILURE, 0, _("cannot combine excl and nocreat"));
  if (multiple_bits_set (input_flags & (O_DIRECT | O_NOCACHE))
      || multiple_bits_set (output_flags & (O_DIRECT | O_NOCACHE)))
    error (EXIT_FAILURE, 0, _("cannot combine direct and nocache"));

  if (input_flags & O_NOCACHE)
    {
      i_nocache = true;
      input_flags &= ~O_NOCACHE;
    }
  if (output_flags & O_NOCACHE)
    {
      o_nocache = true;
      output_flags &= ~O_NOCACHE;
    }
}










// dd if=file of=file bs=BS
// doesn't follow GNU convention
// but remember GNU convention is -k, -k v, --key value
// there's also --key=value but that's not by default
int main (int argc, char * * argv) {

    // default 512 bytes

    puts("Hello!");

    // assigns it statically
    // gets the memory page size
    // _SC_PAGESIZE is some compile time constant (a macro), might be exposed from one of the headers
    // perhaps (unistd.h)
    // this is the number of bytes
    page_size = sysconf (_SC_PAGESIZE);

    // if getopt_long == 1, then the argv contains gnu style options (shortopts and longopts)
    // this is wrong, so we exit, because dd does not accept gnu style options, it has its own stule
    if (getopt_long (argc, argv, "", NULL, NULL)) {
        puts ("Incorrect Options! Do not use GNU style options");
        return EXIT_FAILURE;
    }

    // process dd specific style options (if=.. of=..)
    scanargs (argc, argv);






    return EXIT_SUCCESS;

}
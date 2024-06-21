#include <pj/argparse.h>
#include <pj/errno.h>
#include <pj/list.h>
#include <pj/log.h>
#include <pj/string.h>
#include <pj/unittest.h>

/* Overrideable max tests */
#ifndef UT_MAX_TESTS
#  define UT_MAX_TESTS  16
#endif

/* Overrideable log max buffer size */
#ifndef UT_LOG_BUF_SIZE
#  define UT_LOG_BUF_SIZE  1000
#endif

/* Usually app won't supply THIS_FILE when including test_util.h, so
 * create a temporary one
 */
#ifndef THIS_FILE
#  define THIS_FILE         "test.c"
#  define UNDEF_THIS_FILE   1
#endif

/* Unit testing app */
typedef struct ut_app_t
{
    pj_test_select_tests prm_logging_policy;
    int                  prm_nthreads;
    int                  prm_list_test;
    pj_bool_t            prm_stop_on_error;
    unsigned             flags;

    pj_pool_t           *pool;
    pj_test_suite        suite;
    pj_test_runner      *runner;

    int                  ntests;
    pj_test_case         test_cases[UT_MAX_TESTS];
} ut_app_t;

/* Call this in main.c before parsing arguments */
static void ut_app_init0(ut_app_t *ut_app)
{
    pj_bzero(ut_app, sizeof(*ut_app));
    ut_app->prm_logging_policy = PJ_TEST_FAILED_TESTS;
    ut_app->prm_nthreads = -1;
    ut_app->flags = PJ_TEST_FUNC_NO_ARG;
}

/* Call this in test.c before adding test cases */
static pj_status_t ut_app_init1(ut_app_t *ut_app, pj_pool_factory *mem)
{
    ut_app->pool = pj_pool_create(mem, THIS_FILE, 4000, 4000, NULL);
    PJ_TEST_NOT_NULL(ut_app->pool, NULL, return PJ_ENOMEM);
    pj_test_suite_init(&ut_app->suite);
    return PJ_SUCCESS;
}

/* Don't forget to call this */
static void ut_app_destroy(ut_app_t *ut_app)
{
    pj_pool_release(ut_app->pool);
    ut_app->pool = NULL;
}

#define UT_ADD_TEST(ut_app, test_func, flags) \
            ut_add_test(ut_app, test_func, #test_func, flags, argc, argv)

/* Check if a test is specified/requested in cmdline */
static pj_bool_t ut_test_included(const char *name, int argc, char *argv[])
{
    if (argc <= 1)
        return PJ_TRUE;

    ++argv;
    while (*argv) {
        if (pj_ansi_strcmp(name, *argv)==0)
            return PJ_TRUE;
        ++argv;
    }
    return PJ_FALSE;
}

/* Add test case */
static pj_status_t ut_add_test(ut_app_t *ut_app, int (*test_func)(void),
                               const char *test_name, unsigned flags,
                               int argc, char *argv[])
{
    char *log_buf;
    pj_test_case *tc;

    if (ut_app->ntests >= UT_MAX_TESTS) {
        PJ_LOG(1,(THIS_FILE, "Too many tests for adding %s", test_name));
        return PJ_ETOOMANY;
    }

    if (!ut_test_included(test_name, argc, argv)) {
        return PJ_ENOTFOUND;
    }

    log_buf = (char*)pj_pool_alloc(ut_app->pool, UT_LOG_BUF_SIZE);
    tc = &ut_app->test_cases[ut_app->ntests];
    flags |= ut_app->flags;
    pj_test_case_init(tc, test_name, flags, (int (*)(void*))test_func, NULL,
                      log_buf, UT_LOG_BUF_SIZE, NULL);

    pj_test_suite_add_case( &ut_app->suite, tc);
    ++ut_app->ntests;

    return PJ_SUCCESS;
}

static void ut_list_tests(ut_app_t *ut_app, const char *title)
{
    unsigned d = pj_log_get_decor();
    const pj_test_case *tc, *prev=NULL;

    pj_log_set_decor(d ^ PJ_LOG_HAS_NEWLINE);
    PJ_LOG(3,(THIS_FILE, "%ld %s:", pj_list_size(&ut_app->suite.tests),
              title));
    pj_log_set_decor(0);
    for (tc=ut_app->suite.tests.next; tc!=&ut_app->suite.tests; tc=tc->next) {
        if (!prev || pj_ansi_strcmp(tc->obj_name, prev->obj_name))
            PJ_LOG(3,(THIS_FILE, " %s", tc->obj_name));
        prev = tc;
    }
    PJ_LOG(3,(THIS_FILE, "\n"));
    pj_log_set_decor(d);
}

static pj_status_t ut_run_tests(ut_app_t *ut_app, const char *title,
                                int argc, char *argv[])
{
    pj_test_runner_param runner_prm;
    pj_test_runner_param_default(&runner_prm);
    pj_test_runner *runner;
    pj_test_stat stat;
    pj_status_t status;

    if (ut_app->prm_list_test) {
        ut_list_tests(ut_app, title);
        return PJ_SUCCESS;
    }

    if (argc > 1) {
        int i;
        for (i=1; i<argc; ++i) {
            pj_test_case *tc;
            for (tc=ut_app->suite.tests.next; tc!=&ut_app->suite.tests; 
                 tc=tc->next)
            {
                if (pj_ansi_strcmp(argv[i], tc->obj_name)==0)
                    break;
            }
            if (tc==&ut_app->suite.tests) {
                PJ_LOG(2,(THIS_FILE, "Test \"%s\" is not found in %s",
                          argv[i], title));
            }
        }
    }

    if (ut_app->ntests <= 0)
        return PJ_SUCCESS;

    pj_test_runner_param_default(&runner_prm);
    runner_prm.stop_on_error = ut_app->prm_stop_on_error;
    if (ut_app->prm_nthreads >= 0)
        runner_prm.nthreads = ut_app->prm_nthreads;
    status = pj_test_create_text_runner(ut_app->pool, &runner_prm, &runner);
    PJ_TEST_SUCCESS(status, "error creating text runner", return status);

    PJ_LOG(3,(THIS_FILE,
              "Performing %d %s with %d worker thread%s", 
              ut_app->ntests, title, runner_prm.nthreads, 
              runner_prm.nthreads>1?"s":""));
    pj_test_run(runner, &ut_app->suite);
    pj_test_runner_destroy(runner);
    pj_test_get_stat(&ut_app->suite, &stat);
    pj_test_display_stat(&stat, title, THIS_FILE);
    pj_test_display_log_messages(&ut_app->suite, ut_app->prm_logging_policy);

    return stat.nfailed ? PJ_EBUG : PJ_SUCCESS;
}

static void ut_usage()
{
    puts("  -l 0,1,2,3       0: Don't show logging after tests");
    puts("                   1: Show logs of only failed tests (default)");
    puts("                   2: Show logs of only successful tests");
    puts("                   3: Show logs of all tests");
    puts("  --log-no-cache   Do not cache logging");
    printf("  -w N             Set N worker threads (0: disable. Default: %d)\n", PJ_HAS_THREADS);
    puts("  -L, --list       List the tests and exit");
    puts("  --stop-err       Stop testing on error");
}


static pj_status_t ut_parse_args(ut_app_t *ut_app, int *argc, char *argv[])
{
    int itmp;
    pj_status_t status;

    ut_app->prm_list_test = pj_argparse_get("-L", argc, argv) ||
                            pj_argparse_get("--list", argc, argv);
    ut_app->prm_stop_on_error = pj_argparse_get("--stop-err", argc, argv);
    if (pj_argparse_get("--log-no-cache", argc, argv)) {
        ut_app->flags |= PJ_TEST_LOG_NO_CACHE;
    }

    status = pj_argparse_get_int("-l", argc, argv, &itmp);
    if (status==PJ_SUCCESS && itmp>=0 && itmp<=3) {
        ut_app->prm_logging_policy = (pj_test_select_tests)itmp;
    } else if (status!=PJ_ENOTFOUND) {
        puts("Error: invalid/missing value for -l option");
        return PJ_EINVAL;
    }

    status = pj_argparse_get_int("-w", argc, argv, 
                                 (int*)&ut_app->prm_nthreads);
    if (status==PJ_SUCCESS) {
        if (ut_app->prm_nthreads > 50 || ut_app->prm_nthreads < 0) {
            printf("Error: value %d is not valid for -w option\n", 
                   ut_app->prm_nthreads);
            return PJ_EINVAL;
        }
    } else if (status!=PJ_ENOTFOUND) {
        puts("Error: invalid/missing value for -w option");
        return PJ_EINVAL;
    }
    return PJ_SUCCESS;
}

#ifdef UNDEF_THIS_FILE
#  undef THIS_FILE
#endif

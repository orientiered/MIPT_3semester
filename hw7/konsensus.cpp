#include <iostream>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/wait.h>
#include <algorithm>
#include "../utils.hpp"

struct msg_t
{
    long       mtype; // kMainMtype to send broadcast to everyone
    int        from;  // Set by bogatir, to be identified by main, which is needed to create broadcast messages
    int        value;

    msg_t ( long mtype_, int from_, int value_) : mtype{ mtype_}, from{ from_}, value{ value_} {}
    msg_t () : mtype{ 0}, from{ 0}, value{ 0} {}

};

const size_t kMsgSize     = sizeof( msg_t) - sizeof( msg_t::mtype);
const long   kMainMtype   = 1;

const int    gStartSymbol = ' '; // Fist printing symbol
const int    kLastSymbol  = '~'; // Last printing symbol in 7 bit ascii
const int    kBogatirsNum = kLastSymbol - gStartSymbol + 1;

inline long gen_mtype_from_main( int id) { return 3 + id; }

int get_random_nonzero() {
    srand(time(nullptr) ^ getpid());

    int val = 0;
    do {
        val = rand();
    } while (val == 0);

    return val;
}

bool check_all_unique_sorted(int *values, int len) {
    for (int i = 1; i < len; i++) {
        if (values[i-1] == values[i]) return false;
    }

    return true;
}

void send_broadcast_request(int msg_id, int main_id, int val) {
    msg_t msg{ kMainMtype, main_id, val};
    CHECK(msgsnd( msg_id, &msg, kMsgSize, 0), "msgsnd");
}

int receive_from_broadcast(int msg_id, int main_id) {
    msg_t msg;

    CHECK(msgrcv( msg_id, &msg, kMsgSize, gen_mtype_from_main(main_id), 0), "msgrcv");
    return msg.value;
}

int choose_leader(int msg_id, int id, int len) {
    int myval = 0;

    std::vector<int> values(len);

    while (true) {
        // Generate my random value and send it to everybody
        myval = get_random_nonzero();


        send_broadcast_request(msg_id, id, myval);

        // Get all values
        values[0] = myval;
        for ( int i = 1; i < len; ++i )
        {
            values[i] = receive_from_broadcast(msg_id, id);
        }

        // Sort in ascending order
        std::sort( values.begin(), values.end());

        // Check if all numbers are unique
        if (check_all_unique_sorted(values.data(), len))
            break;


    }

    // All processes generated unique number
    int my_id = -1;
    auto myval_iter = std::find(values.begin(), values.end(), myval);

    if (myval_iter != values.end()) return myval_iter - values.begin();
    else {
        PROC_LOG("Logic error: can't find my id\n");
        return -1;
    }

}


void leader(int msg_id, int id, const char *string, char symbol) {
    for ( const char *pos = string; *pos != '\0'; ++pos ) {
        if ( *pos == symbol )
        {
            std::putchar( symbol);
            std::fflush( stdout);
        } else
        {
            // Command to symbol owner
            send_broadcast_request(msg_id, id, *pos);

            int response_symbol = receive_from_broadcast(msg_id, id);
            if (response_symbol != *pos) {
                PROC_LOG("Unexpected response on symbol\n");
            }
        }
    }

    std::putchar( '\n');
    std::fflush( stdout);

    // Sending exit message
    send_broadcast_request(msg_id, id, 0);
}

void follower(int msg_id, int id, char symbol) {
    for ( ; ; )
    {
        int msg_val = receive_from_broadcast(msg_id, id);

        // exit
        if ( msg_val  == 0 )
            break;

        // wrong worker
        if ( msg_val != symbol )
            continue;

        std::putchar( symbol);
        std::fflush( stdout);

        // response after printing symbol
        send_broadcast_request(msg_id, id, symbol);
    }
}

void worker( int         msg_id,
             int         id,     // Used only to be identified by main functions, which creates broadcast
             int         workerCnt,
             const char *string)
{

    int my_id = choose_leader(msg_id, id, workerCnt);
    // PROC_LOG("My id = %d\n", my_id);

    int symbol = gStartSymbol + my_id;

    if ( my_id == 0 )
    {
        leader(msg_id, id, string, symbol);
    } else
    {
        follower(msg_id, id, symbol);
    }

    // PROC_LOG("Follower: exiting\n");
    exit(EXIT_SUCCESS);

}

void broadcast_helper(int msg_id, int workerCnt) {
    while (true) {
        msg_t msg;
        CHECK(msgrcv( msg_id, &msg, kMsgSize, kMainMtype, 0), "msgrcv failed");

        int from = msg.from;
        msg.from = 0;

        for ( int i = 0; i < workerCnt; ++i )
        {
            if ( i == from )
            {
                continue;
            }

            msg.mtype = gen_mtype_from_main( i);
            CHECK(msgsnd( msg_id, &msg, kMsgSize, 0), "main broadcast msgsnd");
        }
        if ( msg.value == 0 )
        {
            PROC_LOG("Terminate command\n");
            break;
        }
    }
}

int main( int argc, const char *argv[]) {
    if ( argc != 2 ) {
        std::cerr << "Usage: " << argv[0] << " <string to print>" << std::endl;
        return EXIT_FAILURE;
    }

    int msg_id = msgget( IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0666);
    CHECK(msg_id, "Message queue error");

    for ( int i = 0; i < kBogatirsNum; ++i ) {
        SPAWN(worker(msg_id, i, kBogatirsNum, argv[1]););
    }

    broadcast_helper(msg_id, kBogatirsNum);

    PROC_LOG("Waiting for all processes\n");
    wait_for_all();

    msgctl( msg_id, IPC_RMID, 0);

    return EXIT_SUCCESS;
}

#include "../utils.hpp"

#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <set>

#include <pthread.h>
#include <stdio.h>
#include <vector>

// 5b

const int MAX_ENTITIES = 16;

#define MUTEX(mtx, ...) \
    pthread_mutex_lock(mtx); \
    __VA_ARGS__ \
    pthread_mutex_unlock(mtx);

#define lock() pthread_mutex_lock(&mtx)
#define unlock() pthread_mutex_unlock(&mtx)

# define THRD_LOG(fmt, ...) LOG("| %lu | " fmt, pthread_self() % 100 ,##__VA_ARGS__)

const int CRIT_SHIPS = 3;

struct monitor {
    pthread_mutex_t mtx;
    pthread_cond_t bridge_raised, bridge_down;
    pthread_cond_t crit;
    // int car_queue[MAX_ENTITIES];
    // int car_cnt = 0;
    // int ship_queue[MAX_ENTITIES];
    // int ship_cnt = 0;
    std::set<int> cars;
    std::set<int> ships;

    bool raised = false;
    bool empty = true;

    void init() {
        mtx = PTHREAD_MUTEX_INITIALIZER;
        crit = PTHREAD_COND_INITIALIZER;
        bridge_raised = PTHREAD_COND_INITIALIZER;
        bridge_down = PTHREAD_COND_INITIALIZER;

        CHECK(pthread_mutex_init(&mtx, NULL), "mtx init");
        CHECK(pthread_cond_init(&bridge_raised, NULL), "cond init");
        CHECK(pthread_cond_init(&bridge_down, NULL), "cond init");
        CHECK(pthread_cond_init(&crit, NULL), "cond init");
    }

    void random_sleep() {
        usleep( (rand() % 500 + 500) * 1000 );
    }

    void pass_car() {
        THRD_LOG("Машина подъехала к мосту\n");
        lock();

        cars.insert(pthread_self());

        // TODO: кажется кондишн для пустой дороги не нужен, если переезд происходит под мьютексом
        // Ждём двух условий: сведенного моста и открытой дороги
        while (raised) {
            pthread_cond_wait(&bridge_down, &mtx);

            while(!empty) {
                pthread_cond_wait(&crit, &mtx);
            }
            // учитываем, что мост могли развести сразу после того, как с него съехала машина

        }

        empty = false;
        THRD_LOG("Машина дождалась очереди, проезжаем по мосту\n");

        random_sleep();

        THRD_LOG("Мост проехан\n");
        empty = true;
        pthread_cond_broadcast(&crit);

        unlock();
    }

    void pass_ship() {
        lock();
        THRD_LOG("Корабль подплыл к мосту\n");

        ships.insert(pthread_self());

        // Для кораблей гарантируется прохождение под мостом, если он поднят
        while (!raised) {
            if (ships.size() < CRIT_SHIPS) {
                pthread_cond_wait(&bridge_raised, &mtx);
            } else {
                // Критическая секция переезда/переплытия находится в мьютексте,
                // поэтому на мосту гарантированно нет машин
                THRD_LOG("Критическое кол-во кораблей: поднимаем мост\n");
                raised = true;
                // wake up other ships waiting at the bridge
                pthread_cond_broadcast(&bridge_raised);
            }
        }


        while (!empty) {
            pthread_cond_wait(&crit, &mtx);
        }

        //  Критическая секция
        empty = false;
        THRD_LOG("Корабль дождался очереди, проплываем под мостом\n");

        random_sleep();

        THRD_LOG("Корабль прошёл под мостом\n");
        ships.erase(pthread_self());
        empty = true;
        pthread_cond_broadcast(&crit);

        if (ships.size() == 0) {
            THRD_LOG("Все корабли проплыли, сводим мост\n");
            raised = false;
            // waking up all cars waiting at the bridge
            pthread_cond_broadcast(&bridge_down);
        }

        // Конец критической секции

        unlock();
    }


    void destroy() {
        CHECK(pthread_mutex_destroy(&mtx), "mtx destroy");
        CHECK(pthread_cond_destroy(&bridge_raised), "cond destroy");
        CHECK(pthread_cond_destroy(&bridge_down), "cond destroy");
        CHECK(pthread_cond_destroy(&crit), "cond destroy");
    }
};

/* ======================= thread functions ================== */
void* car(void *mon_ptr) {
    monitor *mon = (monitor *) mon_ptr;

    mon->pass_car();

    return NULL;
}

void* ship(void *mon_ptr) {
    monitor *mon = (monitor *) mon_ptr;

    mon->pass_ship();

    return NULL;
}


/* ================= main =========================== */
int main(int argc, const char *argv[]) {

    monitor mon;
    mon.init();

    // spawning threads
    std::vector<pthread_t> tids;
    for (int i = 0; i < 10; i++) {
        pthread_t tid = 0;
        CHECK(pthread_create(&tid, NULL, car, &mon), "thread create");
        tids.push_back(tid);
        // CHECK(pthread_detach(tid), "detach err");
    }

    for (int i = 0; i < 10; i++) {
        pthread_t tid = 0;
        CHECK(pthread_create(&tid, NULL, ship, &mon), "thread create");
        tids.push_back(tid);
        // CHECK(pthread_detach(tid), "detach err");
    }

    LOG("All threads spawned\n");

    for (auto tid: tids) {
        CHECK(pthread_join(tid, NULL), "join error");
    }


    LOG("All threads joined\n");

    mon.destroy();

    return 0;
}

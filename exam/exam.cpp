#include "../utils.hpp"

#include <cstring>
#include <queue>
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


#define MUTEX(mtx, ...) \
    pthread_mutex_lock(mtx); \
    __VA_ARGS__ \
    pthread_mutex_unlock(mtx);

#define lock() pthread_mutex_lock(&mtx)
#define unlock() pthread_mutex_unlock(&mtx)

# define THRD_LOG(fmt, ...) LOG("| %lu | " fmt, pthread_self() % 100 ,##__VA_ARGS__)

const int CRIT_SHIPS = 3;

#pragma region Monitor
struct monitor {
    pthread_mutex_t mtx;
    pthread_cond_t bridge_raised, bridge_down;
    pthread_cond_t cross;
    std::queue<int> *cars;
    std::queue<int> *ships;

    bool raised = false;
    bool empty_bridge = true;
    bool empty_river = true;

    /* ========================== Init, Destroy ========================= */
    void init() {
        mtx = PTHREAD_MUTEX_INITIALIZER;
        cross = PTHREAD_COND_INITIALIZER;
        bridge_raised = PTHREAD_COND_INITIALIZER;
        bridge_down = PTHREAD_COND_INITIALIZER;

        CHECK(pthread_mutex_init(&mtx, NULL), "mtx init");
        CHECK(pthread_cond_init(&bridge_raised, NULL), "cond init");
        CHECK(pthread_cond_init(&bridge_down, NULL), "cond init");
        CHECK(pthread_cond_init(&cross, NULL), "cond init");
    }

    void destroy() {
        CHECK(pthread_mutex_destroy(&mtx), "mtx destroy");
        CHECK(pthread_cond_destroy(&bridge_raised), "cond destroy");
        CHECK(pthread_cond_destroy(&bridge_down), "cond destroy");
        CHECK(pthread_cond_destroy(&cross), "cond destroy");
    }

    /* ======================== Car ============================ */

    void pass_car() {
        lock();

        THRD_LOG("Машина подъехала к мосту\n");
        cars->push(pthread_self());

        // Ждём двух условий: сведенного моста и открытой дороги
        while (raised || !empty_bridge) {
            if (raised) {
                CHECK(pthread_cond_wait(&bridge_down, &mtx), "pthread err");
            }

            if (!empty_bridge) {
                CHECK(pthread_cond_wait(&cross, &mtx), "pthread err");
                // waking up only car at the front of the queue
                if (cars->front() != pthread_self()) continue;
            }
            // учитываем, что мост могли развести сразу после того, как с него съехала машина
        }

        empty_bridge = false;
        THRD_LOG(COL_BLUE "Машина дождалась очереди, проезжаем по мосту\n" COL_RESET);

        unlock();
    }

    void end_car() {
        lock();

        THRD_LOG(COL_GREEN "Мост проехан\n" COL_RESET);
        cars->pop();
        empty_bridge = true;
        CHECK(pthread_cond_broadcast(&cross), "pthread err");

        unlock();
    }

    /* ======================== Ship ============================ */

    void pass_ship() {
        lock();

        THRD_LOG("Корабль подплыл к мосту\n");
        ships->push(pthread_self());

        // Для кораблей гарантируется прохождение под мостом, если он поднят
        while (!raised || !empty_bridge || !empty_river) {
            if (!empty_bridge) {
                CHECK(pthread_cond_wait(&cross, &mtx), "pthread err");
            }

            if (!raised) {
                if (ships->size() < CRIT_SHIPS) {
                    CHECK(pthread_cond_wait(&bridge_raised, &mtx), "pthread err");
                } else {
                    THRD_LOG(COL_BOLD "Критическое кол-во кораблей: поднимаем мост\n" COL_RESET);
                    raised = true;
                    // wake up other ships waiting at the bridge
                    pthread_cond_broadcast(&bridge_raised);
                }
            }

            if (!empty_river) {
                CHECK(pthread_cond_wait(&cross, &mtx), "pthread err");
                // only first ship in queue wakes up
                if (ships->front() != pthread_self()) continue;
            }
        }


        empty_river = false;
        THRD_LOG(COL_BLUE "Корабль дождался очереди, проплываем под мостом\n" COL_RESET);

        unlock();
    }

    void end_ship() {
        lock();

        THRD_LOG(COL_GREEN "Корабль прошёл под мостом\n" COL_RESET);
        ships->pop();
        empty_river = true;

        if (ships->size() == 0) {
            THRD_LOG(COL_BOLD "Все корабли проплыли, сводим мост\n" COL_RESET);
            raised = false;
            // waking up all cars waiting at the bridge
            CHECK(pthread_cond_broadcast(&bridge_down), "pthread err");
        }

        CHECK(pthread_cond_broadcast(&cross), "pthread err");

        unlock();
    }

};

#pragma region Thread fun
/* ======================= thread functions ================== */
void random_sleep() {
    usleep( (rand() % 500 + 500) * 1000 );
}

void* car(void *mon_ptr) {
    monitor *mon = (monitor *) mon_ptr;

    mon->pass_car();
    THRD_LOG("Едем\n");
    random_sleep();
    mon->end_car();

    return NULL;
}

void* ship(void *mon_ptr) {
    monitor *mon = (monitor *) mon_ptr;

    mon->pass_ship();
    THRD_LOG("Плывем\n");
    random_sleep();
    mon->end_ship();

    return NULL;
}

unsigned long spawn_thread(bool type_car,void* mon) {
    pthread_t tid = 0;
    if (type_car) {
        CHECK(pthread_create(&tid, NULL, car, mon), "thread create");
    } else {
        CHECK(pthread_create(&tid, NULL, ship, mon), "thread create");
    }

    return tid;
}

#pragma region Main
/* ================= main =========================== */
int main(int argc, const char *argv[]) {

    monitor mon;
    std::queue<int> cars, ships;
    mon.cars = &cars;
    mon.ships = &ships;
    mon.init();

    // 1 = car
    // 0 = ship
    std::vector<pthread_t> tids;
    std::vector<int> order = {1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1};
    for (auto type: order) {
        tids.push_back(spawn_thread(type, &mon));
    }
    sleep(3);

    order = {0, 0, 0, 0};
    for (auto type: order) {
        tids.push_back(spawn_thread(type, &mon));
    }

    LOG("All threads spawned\n");

    for (pthread_t tid: tids) {
        CHECK(pthread_join(tid, NULL), "join error");
    }

    LOG("All threads joined\n");

    mon.destroy();

    return 0;
}

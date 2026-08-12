#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

extern "C" {
#include "src/worker/queue.h"
}

// Items are opaque pointers; the tests only ever compare them, so fake addresses are enough
static void *item(long n) {
    return (void *) (n + 1);
}

TEST(Queue, FifoOrder) {
    queue_t *queue = queue_create(4);

    for (long i = 0; i < 4; i++) {
        ASSERT_TRUE(queue_push(queue, item(i)));
    }
    ASSERT_EQ(queue_size(queue), 4);

    for (long i = 0; i < 4; i++) {
        ASSERT_EQ(queue_pop(queue), item(i));
    }
    ASSERT_EQ(queue_size(queue), 0);

    queue_destroy(queue);
}

TEST(Queue, WrapsAroundCapacity) {
    queue_t *queue = queue_create(2);

    // More items than the capacity pass through, one at a time
    for (long i = 0; i < 10; i++) {
        ASSERT_TRUE(queue_push(queue, item(i)));
        ASSERT_EQ(queue_pop(queue), item(i));
    }

    queue_destroy(queue);
}

TEST(Queue, TryPopReturnsNullWhenEmpty) {
    queue_t *queue = queue_create(2);

    ASSERT_EQ(queue_try_pop(queue), nullptr);

    queue_push(queue, item(0));

    ASSERT_EQ(queue_try_pop(queue), item(0));
    ASSERT_EQ(queue_try_pop(queue), nullptr);

    queue_destroy(queue);
}

TEST(Queue, PushBlocksWhileFull) {
    queue_t *queue = queue_create(1);

    ASSERT_TRUE(queue_push(queue, item(0)));

    std::atomic<bool> pushed{false};
    std::thread producer([&] {
        queue_push(queue, item(1));
        pushed = true;
    });

    // The producer cannot make progress until something is popped
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_FALSE(pushed.load());

    ASSERT_EQ(queue_pop(queue), item(0));
    producer.join();

    ASSERT_TRUE(pushed.load());
    ASSERT_EQ(queue_pop(queue), item(1));

    queue_destroy(queue);
}

TEST(Queue, PopDrainsBeforeReportingClosed) {
    queue_t *queue = queue_create(4);

    queue_push(queue, item(0));
    queue_push(queue, item(1));
    queue_close(queue);

    ASSERT_EQ(queue_pop(queue), item(0));
    ASSERT_EQ(queue_pop(queue), item(1));
    ASSERT_EQ(queue_pop(queue), nullptr);
    // Still NULL on every subsequent call
    ASSERT_EQ(queue_pop(queue), nullptr);

    queue_destroy(queue);
}

TEST(Queue, PushOnClosedQueueFails) {
    queue_t *queue = queue_create(4);

    queue_close(queue);

    ASSERT_TRUE(queue_is_closed(queue));
    ASSERT_FALSE(queue_push(queue, item(0)));
    ASSERT_EQ(queue_size(queue), 0);

    queue_destroy(queue);
}

TEST(Queue, CloseWakesBlockedConsumers) {
    queue_t *queue = queue_create(4);

    std::atomic<int> woke{0};
    std::vector<std::thread> consumers;
    for (int i = 0; i < 4; i++) {
        consumers.emplace_back([&] {
            ASSERT_EQ(queue_pop(queue), nullptr);
            woke += 1;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(woke.load(), 0);

    queue_close(queue);
    for (auto &consumer: consumers) {
        consumer.join();
    }

    ASSERT_EQ(woke.load(), 4);

    queue_destroy(queue);
}

TEST(Queue, CloseWakesBlockedProducers) {
    queue_t *queue = queue_create(1);

    queue_push(queue, item(0));

    std::atomic<int> rejected{0};
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; i++) {
        producers.emplace_back([&] {
            if (!queue_push(queue, item(1))) {
                rejected += 1;
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue_close(queue);
    for (auto &producer: producers) {
        producer.join();
    }

    ASSERT_EQ(rejected.load(), 4);

    queue_destroy(queue);
}

TEST(Queue, MultiProducerMultiConsumerLosesNothing) {
    const int producer_count = 4;
    const int consumer_count = 4;
    const int per_producer = 2000;

    queue_t *queue = queue_create(8);

    std::atomic<long> consumed_sum{0};
    std::atomic<int> consumed_count{0};

    std::vector<std::thread> consumers;
    for (int i = 0; i < consumer_count; i++) {
        consumers.emplace_back([&] {
            void *popped;
            while ((popped = queue_pop(queue)) != nullptr) {
                consumed_sum += (long) popped;
                consumed_count += 1;
            }
        });
    }

    std::vector<std::thread> producers;
    for (int i = 0; i < producer_count; i++) {
        producers.emplace_back([&, i] {
            for (int n = 0; n < per_producer; n++) {
                ASSERT_TRUE(queue_push(queue, item(i * per_producer + n)));
            }
        });
    }

    for (auto &producer: producers) {
        producer.join();
    }
    queue_close(queue);
    for (auto &consumer: consumers) {
        consumer.join();
    }

    const int total = producer_count * per_producer;
    long expected_sum = 0;
    for (long n = 0; n < total; n++) {
        expected_sum += (long) item(n);
    }

    ASSERT_EQ(consumed_count.load(), total);
    ASSERT_EQ(consumed_sum.load(), expected_sum);

    queue_destroy(queue);
}

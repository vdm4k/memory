#ifndef FIXED_SIZE_MPMC_QUEUE_STRATEGY
#define FIXED_SIZE_MPMC_QUEUE_STRATEGY

#include <atomic>
#include <bit_manip/bit_manip.h>
#include <stddef.h>
#include <string.h>
#include <iostream>
#include <assert.h>

#ifdef _GTEST
#include <gtest/gtest.h>
#endif

typedef uintptr_t uptr;

template<typename T>
class mpmc_bounded_queue_t
{
public:

    mpmc_bounded_queue_t(
            size_t size) :
        _size(size),
        _mask(size - 1),
        _buffer(reinterpret_cast<node_t*>(new aligned_node_t[_size])),
        _head_seq(0),
        _tail_seq(0)
    {
        // make sure it's a power of 2
        assert((_size != 0) && ((_size & (~_size + 1)) == _size));

        // populate the sequence initial values
        for (size_t i = 0; i < _size; ++i) {
            _buffer[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    ~mpmc_bounded_queue_t()
    {
        delete[] _buffer;
    }

    bool
    enqueue(
            const T& data)
    {
        // _head_seq only wraps at MAX(_head_seq) instead we use a mask to convert the sequence to an array index
        // this is why the ring buffer must be a size which is a power of 2. this also allows the sequence to double as a ticket/lock.
        size_t  head_seq = _head_seq.load(std::memory_order_relaxed);

        for (;;) {
            node_t*  node     = &_buffer[head_seq & _mask];
            size_t   node_seq = node->seq.load(std::memory_order_acquire);
            intptr_t dif      = (intptr_t) node_seq - (intptr_t) head_seq;

            // if seq and head_seq are the same then it means this slot is empty
            if (dif == 0) {
                // claim our spot by moving head
                // if head isn't the same as we last checked then that means someone beat us to the punch
                // weak compare is faster, but can return spurious results
                // which in this instance is OK, because it's in the loop
                if (_head_seq.compare_exchange_weak(head_seq, head_seq + 1, std::memory_order_relaxed)) {
                    // set the data
                    node->data = data;
                    // increment the sequence so that the tail knows it's accessible
                    node->seq.store(head_seq + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (dif < 0) {
                // if seq is less than head seq then it means this slot is full and therefore the buffer is full
                return false;
            }
            else {
                // under normal circumstances this branch should never be taken
                head_seq = _head_seq.load(std::memory_order_relaxed);
            }
        }

        // never taken
        return false;
    }

    bool
    dequeue(
            T& data)
    {
        size_t       tail_seq = _tail_seq.load(std::memory_order_relaxed);

        for (;;) {
            node_t*  node     = &_buffer[tail_seq & _mask];
            size_t   node_seq = node->seq.load(std::memory_order_acquire);
            intptr_t dif      = (intptr_t) node_seq - (intptr_t)(tail_seq + 1);

            // if seq and head_seq are the same then it means this slot is empty
            if (dif == 0) {
                // claim our spot by moving head
                // if head isn't the same as we last checked then that means someone beat us to the punch
                // weak compare is faster, but can return spurious results
                // which in this instance is OK, because it's in the loop
                if (_tail_seq.compare_exchange_weak(tail_seq, tail_seq + 1, std::memory_order_relaxed)) {
                    // set the output
                    data = node->data;
                    // set the sequence to what the head sequence should be next time around
                    node->seq.store(tail_seq + _mask + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (dif < 0) {
                // if seq is less than head seq then it means this slot is full and therefore the buffer is full
                return false;
            }
            else {
                // under normal circumstances this branch should never be taken
                tail_seq = _tail_seq.load(std::memory_order_relaxed);
            }
        }

        // never taken
        return false;
    }

private:

    struct node_t
    {
        T                     data;
        std::atomic<size_t>   seq;
    };

    typedef typename std::aligned_storage<sizeof(node_t), std::alignment_of<node_t>::value>::type aligned_node_t;
    typedef char cache_line_pad_t[64]; // it's either 32 or 64 so 64 is good enough

    cache_line_pad_t    _pad0;
    const size_t        _size;
    const size_t        _mask;
    node_t* const       _buffer;
    cache_line_pad_t    _pad1;
    std::atomic<size_t> _head_seq;
    cache_line_pad_t    _pad2;
    std::atomic<size_t> _tail_seq;
    cache_line_pad_t    _pad3;

    mpmc_bounded_queue_t(const mpmc_bounded_queue_t&) {}
    void operator=(const mpmc_bounded_queue_t&) {}
};


template < size_t min_handle_size,
           size_t max_handle_size,
           size_t max_numbers >
class fixed_size_mpmc_queue_strategy
{
public:
    fixed_size_mpmc_queue_strategy()
    {

    }

    ~fixed_size_mpmc_queue_strategy()
    {

    }



    inline void* allocate(std::size_t size) noexcept {
        void * ptr;
        if(curr_size == max_number) {
            std::cout << "Besit blin" <<std::endl;
            return malloc(size);
        }
        if(!queue.dequeue(ptr))
            return malloc(size);
        curr_size++;
        return ptr;
    }

    void deallocate(void* p) noexcept {
        if(p >= begin && p <= end) {
            queue.enqueue(p);
            curr_size--;
        }
        else {
            free(p);
        }
    }

    void init() {
        curr_size = 0;
        memset(begin, 0, array_size);
        uintptr_t ptr = (uintptr_t)begin;
        for (size_t i = 0; i < max_numbers; ++i)
            queue.enqueue((void*)(ptr + i * max_handle_size));
    }


private:

    static const size_t array_size = max_handle_size * max_numbers;
    static uint8_t array[];
    static void* begin;
    static void* end;

    static mpmc_bounded_queue_t<void *> queue;
    static std::atomic_uint_fast64_t curr_size;
    const uint64_t max_number = max_numbers;

#ifdef _GTEST
    FRIEND_TEST(mpmc_bounded_queue, all_address_in_range_one_thread);
    FRIEND_TEST(mpmc_bounded_queue, all_address_in_range_eight_threads);
    FRIEND_TEST(mpmc_bounded_queue, check_concurrency_speed_vs_malloc);
#endif
};

template < size_t min_handle_size,
           size_t max_handle_size,
           size_t max_numbers >
uint8_t fixed_size_mpmc_queue_strategy<min_handle_size, max_handle_size,  max_numbers>::array[fixed_size_mpmc_queue_strategy<min_handle_size, max_handle_size,  max_numbers>::array_size];

template < size_t min_handle_size,
           size_t max_handle_size,
           size_t max_numbers >
void *fixed_size_mpmc_queue_strategy<min_handle_size, max_handle_size,  max_numbers>::begin = fixed_size_mpmc_queue_strategy<min_handle_size, max_handle_size,  max_numbers>::array;

template < size_t min_handle_size,
           size_t max_handle_size,
           size_t max_numbers >
void *fixed_size_mpmc_queue_strategy<min_handle_size, max_handle_size,  max_numbers>::end = &fixed_size_mpmc_queue_strategy<min_handle_size, max_handle_size,  max_numbers>::array[array_size - 1];

template < size_t min_handle_size,
           size_t max_handle_size,
           size_t max_numbers >
std::atomic_uint_fast64_t fixed_size_mpmc_queue_strategy<min_handle_size, max_handle_size, max_numbers>::curr_size(0);

template < size_t min_handle_size,
           size_t max_handle_size,
           size_t max_numbers >
mpmc_bounded_queue_t<void *> fixed_size_mpmc_queue_strategy<min_handle_size, max_handle_size, max_numbers>::queue(max_numbers);


#endif // FIXED_SIZE_MPMC_QUEUE_STRATEGY

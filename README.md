# malloc-lab
Malloc lab from https://csapp.cs.cmu.edu/3e/labs.html

I built four different implementations for a memory allocation library, and sketched a fifth. Performance details in results.txt

### Implicit free-list, first-fit
In this implementation, every block in the heap has a header and a footer with its size, and a flag bit indicating whether it is allocated. Whenever there's a call to malloc, it starts traversing the heap from the beginning, until it finds a free block that's sufficiently large.
- Allocating: time linear in the number of blocks (slow!)
- Freeing: constant time (check if neighbors are free and can be coalesced, update header and footer)
- Memory utilization: coalescing helps, and first-fit is better than next-fit

### Explicit free-list, first-fit
This implementation aimed to improve upon the throughput by traversing only the free blocks, instead of including allocated blocks.
- Allocating: time linear in the number of free blocks (much better for the workloads used)
- Memory utilization: this was now the bottleneck for improving my grade

### Explicit free-list, less footers
This implementation aimed to save 4 bytes for every allocated block by using only a header with no footers. Information about whether the previous block was allocated was encoded in the second LSB, and only free blocks needed footers.

When a block is freed, if its previous neighbor is free, then we must have a way to find the header of the previous block. For that, we rely on the footer that stores the size of the block. If the block following the freed block is free, we can find its header using the current freed block's header, by using its size as an offset. There's no need to know the size of a previous block if it's allocated, so allocated blocks have no footers.
- Memory utilization: this microoptimization made no significant difference, not even a 1% improvement in any of the workloads. It would have, had the allocations been for small ammounts.

### Explicit free-list, best-fit
Since throughput was fine, I experimented with a policy that sacrifices throughput slightly for better memory utilization.
- Allocating: time linear in the number of free blocks. About twice as slow as the first-fit policy, but still within the threshold for a perfect throughput score
- Memory utilization: significantly better, 5% improvement

### Next steps
#### Realloc
I did not have a dedicated realloc implementation, using malloc and free instead. This ended up in much poorer results for the last two workloads, which use realloc. If I were taking a class that uses these tests, I would have implemented realloc using special cases that improve its memory utilization. I found that I wouldn't learn much from doing that, so I stopped short.
#### Segregated free-list
This sounds really cool. It's about having buckets of different sizes, and an explicit free-list for every bucket. This would greatly improve throughput. I probably would implement something along these lines for a more serious allocator.

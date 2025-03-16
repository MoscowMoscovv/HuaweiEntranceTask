# Huawei Entrance Task - Track 7 Performance Library

## Technical Task (Pre-requisite for the Interview)

Consider a server that handles client requests. Each request can either be a **READ** or **WRITE** operation, and each request has the following attributes:
- **ADDRESS**: The starting address where data is to be read or written.
- **SIZE**: The number of bytes to be read or written.

### Server Behavior Rules:
- **Server Occupancy**: The server can process up to **N requests simultaneously**. If a new request arrives when the server is at full capacity, it will not be processed until a request is completed.
- **Ordering Rule**: If a **WRITE** request is active, it blocks any incoming requests that overlap with its address range. The new request will only be processed once the active **WRITE** request is completed.
    - Example: If `REQ1 = WRITE(ADDRESS=100, SIZE=10)` is in progress, and `REQ2 = READ(ADDRESS=105, SIZE=10)` arrives, `REQ2` will be held until `REQ1` is completed.

### Request Processing Time (Latency):
- **WRITE Latency**: `LATENCY = SIZE * BASE_LATENCY`, where `BASE_LATENCY = 1 usec` for **WRITE** requests.
- **READ Latency**: `LATENCY = SIZE * BASE_LATENCY`, where `BASE_LATENCY = 2 usecs` for **READ** requests.

### Tasks:

1. **Server Simulation**:
    - Develop a program that simulates the server's behavior for a pre-recorded request sequence.
    - Use a **trace** from Table 1 as the workload for the simulated server.
    - Test for different values of **N**: `N = 1`, `N = 5`, `N = 10`.

2. **Request Processing Statistics**:
    - Collect and output the following statistics for **READ** and **WRITE** requests:
        - **Median latency**
        - **Average latency**
        - **Minimum latency**
        - **Maximum latency**
    - **Note**: The total latency counting starts at the time the request arrives at the server (using the **Timestamp** column in Table 1).

### Table 1: Request Trace for Server Model Tests

> Note: Please refer to the trace in Table 1 to simulate and calculate the latency statistics.

| Request ID | Timestamp (usec) | Request Type | Address | Size |
|------------|------------------|--------------|---------|------|
| 0          | 3                | READ         | 1024    | 5    |
| 1          | 5                | READ         | 2048    | 5    |
| 2          | 7                | WRITE        | 2048    | 10   |
| 3          | 9                | WRITE        | 2052    | 10   |
| 4          | 12               | READ         | 2048    | 4    |
| 5          | 13               | WRITE        | 1024    | 1    |
| 6          | 15               | READ         | 512     | 10   |
| 7          | 16               | WRITE        | 256     | 20   |
| 8          | 18               | WRITE        | 260     | 5    |
| 9          | 20               | WRITE        | 512     | 7    |
| 10         | 24               | WRITE        | 1024    | 10   |
| 11         | 25               | WRITE        | 1024    | 10   |
| 12         | 26               | WRITE        | 1024    | 10   |
| 13         | 29               | READ         | 512     | 2    |
| 14         | 31               | READ         | 2048    | 15   |
| 15         | 32               | WRITE        | 784     | 6    |
| 16         | 35               | WRITE        | 512     | 3    |
| 17         | 38               | READ         | 256     | 4    |
| 18         | 39               | WRITE        | 256     | 6    |
| 19         | 40               | READ         | 256     | 10   |
| 20         | 41               | READ         | 260     | 5    |
| 21         | 45               | READ         | 270     | 5    |
| 22         | 46               | READ         | 280     | 5    |
| 23         | 47               | WRITE        | 1000    | 20   |
| 24         | 48               | WRITE        | 1010    | 20   |
| 25         | 50               | WRITE        | 1020    | 20   |
| 26         | 55               | READ         | 1000    | 30   |
| 27         | 57               | READ         | 1000    | 30   |
| 28         | 58               | WRITE        | 2052    | 10   |
| 29         | 59               | WRITE        | 2048    | 4    |
| 30         | 60               | READ         | 1024    | 1    |
| 31         | 62               | WRITE        | 512     | 10   |
| 32         | 64               | READ         | 256     | 20   |
| 33         | 68               | WRITE        | 260     | 5    |
| 34         | 70               | WRITE        | 512     | 7    |
| 35         | 71               | READ         | 1024    | 10   |
| 36         | 72               | READ         | 1024    | 10   |
| 37         | 73               | READ         | 1024    | 10   |
| 38         | 74               | READ         | 512     | 2    |
| 39         | 75               | READ         | 2048    | 15   |
| 40         | 76               | WRITE        | 784     | 6    |
| 41         | 77               | WRITE        | 512     | 3    |
| 42         | 78               | WRITE        | 1024    | 10   |
| 43         | 79               | READ         | 1024    | 10   |
| 44         | 82               | READ         | 512     | 2    |
| 45         | 87               | WRITE        | 2048    | 15   |
| 46         | 89               | WRITE        | 784     | 6    |
| 47         | 91               | READ         | 512     | 3    |
| 48         | 95               | WRITE        | 256     | 4    |
| 49         | 96               | WRITE        | 256     | 6    |
```


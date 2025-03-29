#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>
using namespace std;

const int MAX_N = 1e5 + 5;
const int MOD = 1e9 + 7;
const int INF = 1e9;

vector<vector<int>> graph(MAX_N);
vector<int> pathCounts(MAX_N);
vector<int> visited(MAX_N, false);
queue<int> BFS_queue();

// 广度优先搜索函数
void BFS(int N) {
    BFS_queue().push(1);
    pathCounts[1] = 1; // 从节点1开始
    visited[1] = true;

    while (!BFS_queue().empty()) {
        int current = BFS_queue().front();
        BFS_queue().pop();

        for (int neighbor : graph[current]) {
            if (!visited[neighbor]) {
                visited[neighbor] = true;
                BFS_queue().push(neighbor);
                pathCounts[neighbor] += pathCounts[current];
                if(pathCounts[neighbor] >= MOD) pathCounts[neighbor] -= MOD;
            }
        }
    }
}

int main() {
    int N, M;
    cin >> N >> M;

    for (int i = 0; i < M; i++) {
        int from, to;
        cin >> from >> to;
        graph[from].push_back(to); // 向前链接
        graph[to].push_back(from); // 向后链接
    }

    // 进行广度优先搜索，并计算最短路径数量
    BFS(N);

    // 输出最短路径的数量
    for (int i = 2; i <= N; i++){
        cout << (pathCounts[i] % MOD) << endl;
    }

    return 0;
}
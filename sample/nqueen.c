#include <stdio.h>

#define N 8

void print_board(int board[][N]) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++)
            printf(board[i][j] ? "Q " : ". ");
        printf("\n");
    }
    printf("\n\n");
}

int conflict(int board[][N], int row, int col) {
    for (int i = 0; i < row; i++) {
        if (board[i][col])
            return 1;
        int j = row - i;
        if (0 < col - j + 1 && board[i][col - j])
            return 1;
        if (col + j < N && board[i][col + j])
            return 1;
    }
    return 0;
}

void solve(int board[][N], int row) {
    if (row == N) {
        print_board(board);
        return;
    }
    for (int i = 0; i < N; i++) {
        if (!conflict(board, row, i)) {
            board[row][i] = 1;
            solve(board, row + 1);
            board[row][i] = 0;
        }
    }
}

int main(int argc, char **argv) {
    int board[N * N];
    for (int i = 0; i < N * N; i++)
        board[i] = 0;
    solve(board, 0);
    return 0;
}

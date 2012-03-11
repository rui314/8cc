int print_board(int *board) {
  for (int i = 0; i < 8; i = i + 1) {
    for (int j = 0; j < 8; j =j +1) {
      int *v = board + i * 8 + j;
      if (*v) {
        printf("Q ");
      } else {
        printf(". ");
      }
    }
    printf("\n");
  }
  printf("\n\n");
}

int conflict(int *board, int row, int col) {
  for (int i = 0; i < row; i = i + 1) {
    int *v = board + i * 8 + col;
    if (*v) {
      return 1;
    }
    int j = row - i;
    if (0 < col - j + 1) {
      v = board + i * 8 + col - j;
      if (*v) {
        return 1;
      }
    }
    if (col + j < 8) {
      v = board + i * 8 + col + j;
      if (*v) {
        return 1;
      }
    }
  }
  return 0;
}

int solve(int *board, int row) {
  if (row == 8) {
    print_board(board);
    return 0;
  }
  for (int i = 0; i < 8; i = i + 1) {
    if (conflict(board, row, i)) {
      1;
    } else {
      int *v = board + row * 8 + i;
      *v = 1;
      solve(board, row + 1);
      *v = 0;
    }
  }
}

int main() {
  int board[64];
  int i;
  for (i = 0; i < 64; i = i + 1) {
    int *v = board + i;
    *v = 0;
  }
  solve(board, 0);
}

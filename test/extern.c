extern int expect(int, int);
extern int externvar;

int main() {
    expect(99, externvar);
}

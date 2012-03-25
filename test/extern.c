extern int expect(int, int);
extern int externvar1;
int extern externvar2;

int main() {
    expect(98, externvar1);
    expect(99, externvar2);
}

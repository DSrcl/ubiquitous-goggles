// x[i] = 1
unsigned int bitset(unsigned int x, unsigned int i) {
  return x | (1 << i);
}

int main()
{ 
  int i, j;
  volatile int x;

  for (i = 0; i < 5; i++) {
    for (j = 0; j < 5; j++) {
      x = bitset(i, j);
    }
  }
}

unsigned int max(unsigned int a, unsigned int b)
{
  return a > b ? a : b;
}

int main()
{
  volatile int x;
  int i, j;
  for (i = 0; i < 10; i++) {
    for (j = i+1; j < 10; j++) {
      x = max(i, j);
      x = max(j, i);
    }
  }
  return 0;
}

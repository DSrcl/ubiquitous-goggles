int mul(int a, int b)
{
  return a * b;
}

int main()
{ 
  volatile int x;
  int i, j;
  for (i = 200; i < 210; i++) {
    for (j = i+1; j < 210; j++) {
      x = mul(i, j);
    }
  }
  return 0;
}

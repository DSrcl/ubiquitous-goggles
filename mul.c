int mul(int a, int b)
{
  return a * b;
}

int main()
{ 
  volatile int product = 1; 
  int i;
  for (i = 1; i <= 10; i++) {
    product = mul(i-1, i);
  }
  return 0;
}

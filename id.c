int id(int x)
{
  return x;
}

int main()
{
  volatile int x; 
  int i;
  for (i = 0; i < 10; i++) {
    x = id(i);
  }
}

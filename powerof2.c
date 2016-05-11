int powerof2(unsigned int v)
{
  return v && !(v & (v - 1));
}

int main()
{
  volatile int x;
  x = powerof2(16);
  x = powerof2(32);
  x = powerof2(24);
  x = powerof2(8);
  x = powerof2(10);
  x = powerof2(12);
  x = powerof2(128);
}

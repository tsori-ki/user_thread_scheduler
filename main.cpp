#include "uthreads.h"

#include <iostream>

void f (void)
{
	std::cout << "f" << uthread_get_tid() << std::endl;
	int tid = uthread_get_tid();
	int i = 1;
	while(1)
	{
		if(i == uthread_get_quantums(tid))
		{
			std::cout << "f" << tid << " Quanta:" <<  i << std::endl;
			if (i == 5)
			{
				std::cout << "f END" << std::endl;
				uthread_terminate(tid);
			}
			i++;
		}

	}
}

void g (void)
{
	std::cout << "g" << uthread_get_tid() << " Quanta:" <<  0 << std::endl;
	int tid = uthread_get_tid();
	int i = 1;
	while(1)
	{
		if(i == uthread_get_quantums(tid))
		{
			std::cout << "g" << tid << " Quanta:" <<  i << std::endl;
			if (i == 5)
			{
				std::cout << "g END" << std::endl;
				uthread_terminate(tid);
			}
			i++;
		}

	}
}


int main(void)
{
	try
	{
		uthread_init(2000);
		int tid = uthread_get_tid();
		int i = 1;
		std::cout << "Thread:m Number:(0) " << tid << std::endl;
		std::cout << "Init Quantum num is: " << uthread_get_total_quantums() << std::endl;
		while(1)
		{
			if(i == uthread_get_quantums(tid))
			{
				std::cout << "m" << tid << " Quanta:" <<  i << std::endl;
				if (i == 3)
				{
					std::cout << "m spawns f at (1) " << uthread_spawn(f) << std::endl;
					std::cout << "m spawns g at (2) " << uthread_spawn(g) << std::endl;
				}
				if (i == 10)
				{
					std::cout << "Total Quantums: " << uthread_get_total_quantums() << std::endl;
					uthread_terminate(tid);
				}
				i++;
			}

		}
		std::cout << "m END" << std::endl;
	}
	catch(const std::exception& e)
	{
		std::cout << "Caught The Following Excpetion: \n" << e.what() << std::endl;
	}
	std::cout << "End of main" << std::endl;
	return 0;
	// std::cout << "End of main" << std::endl;
}

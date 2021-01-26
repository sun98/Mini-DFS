#include <iostream>
#include <thread>
#include <vector>
#include "namenode.h"
#include "datanode.h"

const int total_copy = 4;

int main()
{
    NameServer ns(total_copy);
    DataServer ds1("data_node_1");
    DataServer ds2("data_node_2");
    DataServer ds3("data_node_3");
    DataServer ds4("data_node_4");

    ns.add(&ds1);
    ns.add(&ds2);
    ns.add(&ds3);
    ns.add(&ds4);

    std::thread th1(std::ref(ds1));
    std::thread th2(std::ref(ds2));
    std::thread th3(std::ref(ds3));
    std::thread th4(std::ref(ds4));

    th1.detach();
    th2.detach();
    th3.detach();
    th4.detach();

    ns();

    return 0;
}

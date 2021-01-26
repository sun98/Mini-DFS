#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include "namenode.h"
#include "md5.h"

NameServer::NameServer(int numReplicate) : numReplicate_(numReplicate), idCnt_(0) {}

void NameServer::add(DataServer *server)
{
    dataServers_.push_back(server);
}

std::vector<std::string> NameServer::parse_cmd()
{
    std::cout << "Mini-DFS> ";
    std::string cmd, tmp;
    std::getline(std::cin, cmd);
    std::vector<std::string> parameters;
    std::stringstream ss(cmd);
    while (ss >> tmp)
        parameters.push_back(tmp);
    return parameters;
}

void NameServer::operator()()
{
    while (true)
    {
        std::vector<std::string> parameters = parse_cmd();
        std::vector<int> idx;
        std::ifstream is;
        char *buf = nullptr;
        // md5 checksum for replicate chunks;
        MD5 md5;
        if (parameters.empty())
        {
            std::cerr << "input a blank line" << std::endl;
            continue;
        }
        if (parameters[0] == "quit" || parameters[0] == "exit")
        {
            exit(0);
        }
        // list all the files in name server.
        else if (parameters[0] == "ls")
        {
            if (parameters.size() != 1)
            {
                std::cerr << "Usage: "
                          << "ls (list all files in name server)" << std::endl;
            }
            else
            {
                std::cout << "File ID\tChunk Num\tFile Name" << std::endl;
                fileTree_.list(meta);
            }
            continue;
        }
        // upload file to miniDFS
        else if (parameters[0] == "up")
        {
            if (parameters.size() != 3)
            {
                std::cerr << "Usage: "
                          << "up src dest" << std::endl;
                continue;
            }
            is.open(parameters[1], std::ifstream::ate | std::ifstream::binary);
            if (!is)
            {
                std::cerr << "Failed in opening src file: file " << parameters[1] << std::endl;
                continue;
            }
            else if (!fileTree_.insert_node(parameters[2], true))
            {
                std::cerr << "Failed in creating dest file.\nPlease ensure File: " << parameters[2] << " does not exist" << std::endl;
                continue;
            }
            else
            {
                int totalSize = is.tellg();
                buf = new char[totalSize];
                is.seekg(0, is.beg);
                is.read(buf, totalSize);
                std::vector<double> serverSize;
                for (const auto &server : dataServers_)
                    serverSize.push_back(server->size());
                idx = argsort<double>(serverSize);
                // std::cout << "total size " << totalSize << std::endl;
                ++idCnt_;
                for (int i = 0; i < numReplicate_; ++i)
                {
                    std::unique_lock<std::mutex> lk(dataServers_[idx[i]]->mtx);
                    meta[parameters[2]] = std::make_pair(idCnt_, totalSize);
                    dataServers_[idx[i]]->cmd = "up";
                    dataServers_[idx[i]]->fid = idCnt_;
                    dataServers_[idx[i]]->bufSize = totalSize;
                    dataServers_[idx[i]]->buf = buf;
                    dataServers_[idx[i]]->finish = false;
                    lk.unlock();
                    dataServers_[idx[i]]->cv.notify_all();
                }
            }
        }
        // fetch file from miniDFS
        else if (parameters[0] == "down" || parameters[0] == "down_id")
        {
            if (parameters.size() != 3 && parameters.size() != 4)
            {
                std::cerr << "Usage: "
                          << "down src dest" << std::endl;
                std::cerr << "Usage: "
                          << "down_id file_id offset dest" << std::endl;
                continue;
            }
            else
            {
                if (parameters[0] == "down" && meta.find(parameters[1]) == meta.end())
                {
                    std::cerr << "Error: no such file in MiniDFS." << std::endl;
                    continue;
                }
                for (int i = 0; i < 4; ++i)
                {
                    std::unique_lock<std::mutex> lk(dataServers_[i]->mtx);
                    dataServers_[i]->cmd = parameters[0];
                    if (parameters[0] == "down")
                    {
                        std::pair<int, int> metaData = meta[parameters[1]];
                        dataServers_[i]->fid = metaData.first;
                        dataServers_[i]->bufSize = metaData.second;
                    }
                    else
                    {
                        dataServers_[i]->fid = std::stoi(parameters[1]);
                        dataServers_[i]->offset = std::stoi(parameters[2]);
                    }
                    dataServers_[i]->finish = false;
                    lk.unlock();
                    dataServers_[i]->cv.notify_all();
                }
            }
        }
        // locate the data server given file ID and Offset.
        else if (parameters[0] == "loc")
        {
            if (parameters.size() != 3)
            {
                std::cerr << "Usage: "
                          << "loc file_id offset" << std::endl;
                continue;
            }
            else
            {
                for (int i = 0; i < 4; ++i)
                {
                    std::unique_lock<std::mutex> lk(dataServers_[i]->mtx);
                    dataServers_[i]->cmd = "loc";
                    dataServers_[i]->fid = std::stoi(parameters[1]);
                    dataServers_[i]->offset = std::stoi(parameters[2]);
                    dataServers_[i]->finish = false;
                    lk.unlock();
                    dataServers_[i]->cv.notify_all();
                }
            }
        }
        else
            std::cerr << "Unknown command." << std::endl;

        // waiting for the finish of data server.
        for (const auto &server : dataServers_)
        {
            std::unique_lock<std::mutex> lk(server->mtx);
            (server->cv).wait(lk, [&]() { return server->finish; });
            lk.unlock();
            (server->cv).notify_all();
        }

        // work after processing of data server
        if (parameters[0] == "down" || parameters[0] == "down_id")
        {
            std::string md5_checksum, pre_checksum;
            for (int i = 0; i < 4; ++i)
            {
                if (dataServers_[i]->bufSize)
                {
                    std::ofstream os;
                    if (parameters[0] == "down")
                        os.open(parameters[2]);
                    else if (parameters[0] == "down_id")
                        os.open(parameters[3]);
                    if (!os)
                        std::cerr << "Failed in creating file.\nPlease ensure dest directory exists." << std::endl;
                    else
                    {
                        os.write(dataServers_[i]->buf, dataServers_[i]->bufSize);
                        os.close();
                        md5.update(dataServers_[i]->buf, dataServers_[i]->bufSize);
                        md5.finalize();
                        md5_checksum = md5.toString();
                        if (!pre_checksum.empty() && pre_checksum != md5_checksum)
                        {
                            std::cerr << "Error: inconsistent checksum for files from different data servers. File may be corrupted." << std::endl;
                        }
                        else if (!pre_checksum.empty() && pre_checksum == md5_checksum)
                        {
                            std::cout << "MD5 checksum passed." << std::endl;
                        }
                        pre_checksum = md5_checksum;
                    }
                    delete[] dataServers_[i]->buf;
                }
            }
        }
        else if (parameters[0] == "up")
        {
            std::cout << "Upload success. File ID is " << idCnt_ << std::endl;
        }
        else if (parameters[0] == "loc" || parameters[0] == "ls")
        {
            bool notFound = true;
            for (int i = 0; i < 4; ++i)
            {
                if (dataServers_[i]->bufSize)
                {
                    notFound = false;
                    std::cout << "Found File ID=" << parameters[1] << ", offset=" << parameters[2] << " at " << dataServers_[i]->get_name() << std::endl;
                }
            }
            if (notFound)
                std::cout << "Error, File ID=" << parameters[1] << ", offset=" << parameters[2] << " not found." << std::endl;
        }
        delete[] buf;
        is.close();
    }
}

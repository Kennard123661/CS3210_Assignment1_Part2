//
// Created by kennard on 07/10/18.
//
#include <openmpi/mpi.h>
#include <queue>
#include <list>
#include <mpi.h>

using namespace std;

int main(void) {
    const unsigned int STATUS_NOT_PROCESSING = 0;
    const unsigned int STATUS_IS_PROCESSING = 2;
    const unsigned int NUM_LINES = 3;

    srand(static_cast<unsigned int>(time(NULL)));
    MPI_Comm centralComm;

    int info[3] = {-1,-1,-1};

    MPI_Init(NULL, NULL);
    MPI_Comm_get_parent(&centralComm);
    int worldRank, childRank;

    MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
    MPI_Comm_rank(centralComm, &childRank);

    // Receive the number of station sides
    unsigned int numStationSides(0);
    MPI_Bcast(&numStationSides, 1, MPI_UINT32_T, 0, centralComm);
    // cout << childRank << " " << numStationSides << endl;
    // Get the node info. Different semantic meaning depending on whether it is a link or a stationSide. If it is
    // a link, then it represents (startStationSide, endStationSide, cost). Otherwise, it represents (link[green],
    // link[yellow], link[blue]), link[green] represents the rank of the link in the intracomm that will green
    // trains passing the station will go to.
    unsigned char isLink(worldRank >= numStationSides);
    MPI_Recv(info, 3, MPI_INT, 0, 0, centralComm, NULL);

    int prevNodes[3] = {-1,-1,-1};
    if (!isLink) {
        MPI_Recv(prevNodes, 3, MPI_INT, 0, 0, centralComm, NULL);
        // cout << prevNodes[0] << " " << prevNodes[1] << " " << prevNodes[2] << endl;
        // cout << info[0] << " " << info[1] << " " << info[2] << endl;
    }

    // Receive the number of ticks
    unsigned int numTicks;
    MPI_Bcast(&numTicks, 1, MPI_UINT32_T, 0, centralComm);
    MPI_Barrier(centralComm);


    bool isTerminal(false);
    float stationPopularity(0);
    list<pair<unsigned int, unsigned int>> beforeProcessing;

    if (!isLink) {
        // the station is a terminal if at least one of its nodes is another station side.
        for (unsigned int i = 0; i < 3; i++) {
            isTerminal |= info[i] < numStationSides;
        }

        MPI_Recv(&stationPopularity, 1, MPI_FLOAT, 0, 0, centralComm, NULL);
        // Populate if it is a starting station
        unsigned int numTrainsFromCentral(0);
        MPI_Recv(&numTrainsFromCentral, 1, MPI_UINT32_T, 0, 0, centralComm, NULL);


        if (numTrainsFromCentral > 0) {
            unsigned numTrainDataFromCentral = numTrainsFromCentral * 2;
            // cout << numTrainDataFromCentral << " " << worldRank << endl;
            unsigned int *trainsFromCentral = (unsigned int *) malloc(sizeof(unsigned int) * numTrainDataFromCentral);
            MPI_Recv(trainsFromCentral, numTrainsFromCentral * 2, MPI_UINT32_T, 0, 0, centralComm, NULL);

            for (unsigned int i = 0; i < numTrainsFromCentral; i++) {
                beforeProcessing.push_back(
                        pair<unsigned int, unsigned int>(trainsFromCentral[i*2], trainsFromCentral[i*2 + 1]));
            }

            free(trainsFromCentral);
            trainsFromCentral = NULL;
        }
    }
    MPI_Barrier(centralComm);


    if (!isLink) {
        // cout << "next " << worldRank << " " << info[0] << " " << info[1] << " " << info[2];
    }

    if (!isLink) {
        // cout << "before " << worldRank << " " << info[0] << " " << info[1] << " " << info[2];
    }

    float countDown(0);
    unsigned int processedTrainInfo[2];
    unsigned int receivedTrainInfo[6];
    bool isProcessing = false;

    unsigned int totalWaitTime[NUM_LINES] = {0};
    unsigned int minWaitTime[NUM_LINES] = {0};
    unsigned int maxWaitTime[NUM_LINES] = {0};
    unsigned int numTrainsVisited[NUM_LINES] = {0};
    unsigned int prevTimeStamp[NUM_LINES] = {0,0,0};

    for (unsigned int t = 0; t < numTicks; t++) {
        unsigned char hasTrainToSend(0);

        // if has previously finished processing
        if (isProcessing == true && countDown <= 0) {
            // cout << "released" << endl;
            isProcessing = STATUS_NOT_PROCESSING;
            pair<unsigned int, unsigned int> processedTrain = beforeProcessing.front();
            processedTrainInfo[0] = processedTrain.first;
            processedTrainInfo[1] = processedTrain.second;

            // If station, update wait times
            if (!isLink) {
                prevTimeStamp[processedTrain.first] = t;
            }

            beforeProcessing.pop_front();
            hasTrainToSend = true;
        }
        MPI_Barrier(MPI_COMM_WORLD);
        // cout << 'a' << endl;

        // Terminals and links will send their trains first. All stations will receive trains
        if (isLink) {
            MPI_Send(&hasTrainToSend, 1, MPI_UNSIGNED_CHAR, info[1], 0, MPI_COMM_WORLD);
            if (hasTrainToSend) {
                MPI_Send(processedTrainInfo, 2, MPI_UINT32_T, info[1], 0, MPI_COMM_WORLD);
            }
        } else {
            if (isTerminal) {
                for (unsigned int i = 0; i < NUM_LINES; i++) {
                    if (info[i] < 0) {
                        continue;
                    }

                    if (i == processedTrainInfo[0] && hasTrainToSend) {
                        MPI_Send(&hasTrainToSend, 1, MPI_UNSIGNED_CHAR, info[i], 0, MPI_COMM_WORLD);
                        MPI_Send(processedTrainInfo, 2, MPI_UINT32_T, info[i], 0, MPI_COMM_WORLD);
                    } else {
                        unsigned char noTrain(0);
                        MPI_Send(&noTrain, 1, MPI_UNSIGNED_CHAR, info[i], 0, MPI_COMM_WORLD);
                    }
                }
            }

            // Notice that a station can have a maximum in-degree of 3 links. Hence, can receive up to 3 trains
            // for each link
            for (unsigned int i = 0; i < NUM_LINES; i++) {
                if (prevNodes[i] < 0) {
                    continue;
                }

                unsigned char hasTrainToReceive(0);
                MPI_Recv(&hasTrainToReceive, 1, MPI_UNSIGNED_CHAR, prevNodes[i], 0, MPI_COMM_WORLD, NULL);
                if (hasTrainToReceive) {
                    MPI_Recv(receivedTrainInfo, 2, MPI_INT32_T, prevNodes[i], 0, MPI_COMM_WORLD, NULL);
                    beforeProcessing.push_back(
                            pair<unsigned int, unsigned int>(receivedTrainInfo[0], receivedTrainInfo[1]));
                }
            }
        }
        MPI_Barrier(MPI_COMM_WORLD);
        // cout << 'b' << endl;


        // non-terminal trains will now receive
        if (isLink) {
            unsigned char hasTrainToReceive(0);
            MPI_Recv(&hasTrainToReceive, 1, MPI_UNSIGNED_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, NULL);
            if (hasTrainToReceive) {
                // cout << "hasTransferred" << endl;
                MPI_Recv(receivedTrainInfo, 2, MPI_INT32_T, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, NULL);
                beforeProcessing.push_back(
                        pair<unsigned int, unsigned int>(receivedTrainInfo[0], receivedTrainInfo[1]));
            }
        } else if (!isTerminal) {
            for (unsigned int i = 0; i < NUM_LINES; i++) {
                if (info[i] < 0) {
                    continue;
                }

                if (i == processedTrainInfo[0] && hasTrainToSend) {
                    MPI_Send(&hasTrainToSend, 1, MPI_UNSIGNED_CHAR, info[i], 0, MPI_COMM_WORLD);
                    MPI_Send(processedTrainInfo, 2, MPI_UINT32_T, info[i], 0, MPI_COMM_WORLD);
                } else {
                    unsigned char noTrain(0);
                    MPI_Send(&noTrain, 1, MPI_UNSIGNED_CHAR, info[i], 0, MPI_COMM_WORLD);
                    // cout << worldRank << endl;
                }
            }
        }
        MPI_Barrier(MPI_COMM_WORLD);

        // cout << 'c' << endl;

        // Actual processing
        if ((isProcessing == false) && !beforeProcessing.empty()) {
            if (isLink) {
                countDown = info[2];
            } else {
                countDown = stationPopularity * ((rand() % 10) + 1);
                unsigned int processedTrainLineIdx = beforeProcessing.front().first;
               // cout << processedTrainLineIdx << endl;
                unsigned int waitTime = t - prevTimeStamp[processedTrainLineIdx];
                totalWaitTime[processedTrainLineIdx] += waitTime;
                minWaitTime[processedTrainLineIdx] = min(minWaitTime[processedTrainLineIdx], waitTime);
                maxWaitTime[processedTrainLineIdx] = max(maxWaitTime[processedTrainLineIdx], waitTime);
                numTrainsVisited[processedTrainLineIdx]++;
            }

            isProcessing = true;
        } else if (isProcessing) {
            countDown -= 1;
        }
        MPI_Barrier(MPI_COMM_WORLD);
        // cout << 'd' << endl;

        // Update master process.
        unsigned int numTrains(beforeProcessing.size());
        MPI_Send(&numTrains, 1, MPI_UINT32_T, 0, 0, centralComm);

        if (numTrains > 0) {
            unsigned int *holdingTrains = static_cast<unsigned int *>(malloc(sizeof(unsigned int) * numTrains * 2));
            unsigned int idx(0);
            for (list<pair<unsigned int, unsigned int>>::iterator it = beforeProcessing.begin();
                 it != beforeProcessing.end(); it++) {
                pair<unsigned int, unsigned int> train = *it;
                // cout << train.first << "," << train.second << endl;
                holdingTrains[idx++] = train.first;
                holdingTrains[idx++] = train.second;
            }

            MPI_Send(holdingTrains, numTrains * 2, MPI_UINT32_T, 0, 0, centralComm);
        }
        MPI_Barrier(centralComm);

/*        free(holdingTrains);
        holdingTrains = NULL;*/
    }

    /*
    // update master process with values
    if (!isLink) {
        const unsigned int NUM_DATA_TO_SEND_PER_LINE = 4;
        unsigned int waitData[NUM_DATA_TO_SEND_PER_LINE * NUM_LINES];
        for (unsigned int i = 0; i < NUM_LINES; i++) {
            waitData[i * 4] = totalWaitTime[i];
            waitData[i * 4 + 1] = minWaitTime[i];
            waitData[i * 4 + 2] = maxWaitTime[i];
            waitData[i * 4 + 3] = numTrainsVisited[i];
        }

        MPI_Send(&waitData, NUM_DATA_TO_SEND_PER_LINE * NUM_LINES, MPI_UINT32_T, 0, 0, centralComm);
    }
    MPI_Barrier(centralComm);*/
    MPI_Finalize();

    return EXIT_SUCCESS;
}

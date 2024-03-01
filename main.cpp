#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <dirent.h>
#include <sstream>
#include <vector>
#include <fstream>
#include <sys/stat.h>
#include <thread>
#include <mutex>

using namespace std;

mutex m;

enum class BoardState {
    EMPTY,
    X,
    O
};

class Server {
private:
    int serverSocket;
    int port;
    sockaddr_in serverAddr{};
    vector<std::thread> clientThreads;
    vector<BoardState> board;
    vector<int> clients;
    int turn = 0;


    enum class GameStatus {
        ONGOING,
        X_WINS,
        O_WINS,
        DRAW
    };

    GameStatus checkStatus() {
//        lock_guard<mutex> lock(m);
        for (int i = 0; i < 3; ++i) {
            if (board[3*i] == board[3*i + 1] && board[3*i] == board[3*i + 2]) {
                if (board[3*i] == BoardState::X) return GameStatus::X_WINS;
                if (board[3*i] == BoardState::O) return GameStatus::O_WINS;
            }
        }

        for (int i = 0; i < 3; ++i) {
            if (board[i] == board[i + 3] && board[i] == board[i + 6]) {
                if (board[i] == BoardState::X) return GameStatus::X_WINS;
                if (board[i] == BoardState::O) return GameStatus::O_WINS;
            }
        }

        if (board[0] == board[4] && board[0] == board[8]) {
            if (board[0] == BoardState::X) return GameStatus::X_WINS;
            if (board[0] == BoardState::O) return GameStatus::O_WINS;
        }
        if (board[2] == board[4] && board[2] == board[6]) {
            if (board[2] == BoardState::X) return GameStatus::X_WINS;
            if (board[2] == BoardState::O) return GameStatus::O_WINS;
        }

        if (std::find(board.begin(), board.end(), BoardState::EMPTY) == board.end()) {
            return GameStatus::DRAW;
        }


        return GameStatus::ONGOING;
    }

    void checkwinner(){
        GameStatus gameStatus = checkStatus();
        if (gameStatus == GameStatus::X_WINS || gameStatus == GameStatus::O_WINS || gameStatus == GameStatus::DRAW) {
            string message;
            switch (gameStatus) {
                case GameStatus::X_WINS:
                    message = "X wins";
                    break;
                case GameStatus::O_WINS:
                    message = "O wins";
                    break;
                case GameStatus::DRAW:
                    message = "Draw";
                    break;
                default:
                    break;
            }
//            lock_guard<mutex> lock2(m);
            for (int client : clients) {
                send(client, message.c_str(), message.size(), 0);
                send(client, "QUIT", 4, 0);
//                close(client);
            }
            clients.clear();
        }
    }

    void createBoard(){
//        lock_guard<mutex> lock(m);
        board.resize(9);
        for (int i = 0; i < 9; i++) {
            board[i] = BoardState::EMPTY;
        }
    }

    bool makemove(int clientSocket, int row, int col) {

        int index = 3 * row + col;
        auto clientIter = find(clients.begin(), clients.end(), clientSocket);
        if (clientIter == clients.end()) {
            return false;
        } else if (clientIter == clients.end() || board[index] != BoardState::EMPTY || (clientIter - clients.begin()) != turn) {
            return false;
        } else if (turn == 0) {
            board[index] = BoardState::X;
        } else {
            board[index] = BoardState::O;
        }

        updateboard(clientSocket);
        checkwinner();
        m.lock();
        turn = (turn + 1) % 2;
        m.unlock();
        return true;
    }



    void updateboard(int clientSocket) {
//        lock_guard<mutex> lock(m);
        string boardState;
        for (int i = 0; i < 9; i++) {
            switch (board[i]) {
                case BoardState::X:
                    boardState += "X    ";
                    break;
                case BoardState::O:
                    boardState += "O    ";
                    break;
                default:
                    boardState += "*    ";
                    break;
            }
            cout << i % 3;
            if (i % 3 == 2) {
                boardState += "\n\n";
            }
        }
//        lock_guard<mutex> lock2(m);
        for (int client : clients) {
            send(client, boardState.c_str(), boardState.size(), 0);
        }
    }

    void bindAndListen() {
        if (::bind(serverSocket, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) == -1) {
            perror("Bind failed");
            exit(1);
        }


        if (listen(serverSocket, SOMAXCONN) == -1) {
            perror("Listen failed");
            exit(1);
        }

        std::cout << "Server listening on port " << port << std::endl;
    }


    int acceptClient() {
        sockaddr_in clientAddr{};
        socklen_t clientAddrLen = sizeof(clientAddr);

//        lock_guard<mutex> lock2(m);
        int clientSocket = accept(serverSocket, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientAddrLen);
        if (clientSocket == -1) {
            perror("Accept failed");
            return -1;
        }
//        m.lock();
        std::cout << "Accepted connection from " << inet_ntoa(clientAddr.sin_addr) << std::endl;
//        m.unlock();
        return clientSocket;
    }



    void handleClient(int clientSocket){
        char buffer[1024];

        memset(buffer, 0, sizeof(buffer));
        ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            close(clientSocket);
            return;
        }
        string bufferSTR = string(buffer);

        if (bufferSTR == "START"){
            string message = "Welcome";
            send(clientSocket, message.c_str(), message.size(), 0);
            while (true) {
                memset(buffer, 0, sizeof(buffer));
                bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
                if (bytesReceived <= 0) {
                    break;
                }

                string bufferStr(buffer);

                if (bufferStr == "QUIT") {
                    string message = "QUIT";
                    send(clientSocket, message.c_str(), message.size(), 0);
                    break;
                } else if (bufferStr.find("MOVE") == 0){
                    string delimiter = " ";
                    size_t pos = bufferStr.find(delimiter) + 1;
                    string rowStr = bufferStr.substr(pos, bufferStr.find(delimiter, pos) - pos);
                    pos = bufferStr.find(delimiter, pos) + 1;
                    string columnStr = bufferStr.substr(pos);
                    int row = stoi(rowStr);
                    int column = stoi(columnStr);
                    if (!makemove(clientSocket, row, column)){
                        string message = "Invalid move";
                        send(clientSocket, message.c_str(), message.size(), 0);
                    }
                }
            }
            close(clientSocket);
        } else {
            string message = "Invalid command";
            send(clientSocket, message.c_str(), message.size(), 0);
        }
    }


public:
    Server(int port) : port(port) {
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == -1) {
            perror("Error during creating socket");
            exit(1);
        }

        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port);
    }

    ~Server() {
        close(serverSocket);
    }

    void disconnect() {
        close(serverSocket);
//        m.lock();
        std::cout << "Server disconnected" << std::endl;
//        m.unlock();
    }

    void start() {
        createBoard();
        bindAndListen();


        while (true) {
            int clientSocket = acceptClient();

            if (clientSocket == -1) {
                break;
            }
            if  (clientThreads.size() >= 2){
//                m.lock();
                cout << "Too many gamers" << endl;
//                m.unlock();
                close(clientSocket);
                continue;
            } else {

//                lock_guard<mutex> lock2(m);
                clients.push_back(clientSocket);
                clientThreads.emplace_back([this, clientSocket](){ handleClient(clientSocket); });
                clientThreads.erase(
                        remove_if(clientThreads.begin(), clientThreads.end(),
                                  [](std::thread &t) { return !t.joinable(); }),
                        clientThreads.end());
            }
        }
        for (auto& t : clientThreads) {
            if (t.joinable()) {
                t.join();
            }
        }

        disconnect();
    }
};

int main() {
    int port = 12345;
    Server server(port);
    server.start();
    return 0;
}
/*******************************************
 * main.cpp
 *******************************************/
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/throw_exception.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include "httplib.h"       
#include "inja/inja.hpp"   
#include "include/exchange.h" 

using namespace std;
using namespace httplib;
using json          = nlohmann::json;
namespace beast     = boost::beast;
namespace websocket = boost::beast::websocket;
namespace asio      = boost::asio;

boost::wrapexcept<std::runtime_error> myWrappedEx(std::runtime_error("Some error"));

//------------------------------------
// GLOBALS
//------------------------------------
Exchange session(3);
static unordered_map<string,int> traderIDs;
static boost::asio::io_context g_ioc;
static std::unique_ptr<boost::asio::ip::tcp::acceptor> g_acceptor;

// A list of active WebSocket connections for live updates
// mutex for thread safety.
static vector<shared_ptr<websocket::stream<beast::tcp_stream>>> activeSessions;
static mutex sessionMutex;

static unordered_map<websocket::stream<beast::tcp_stream>*, string> sessionToUserMap;
static mutex mapMutex;


/**
 * Broadcast a string message (typically JSON) to all connected WebSocket sessions.
 */
void broadcastMessage(const string &msg) {
    lock_guard<mutex> lock(sessionMutex);
    for (auto &ws : activeSessions) {
        if (ws && ws->is_open()) {
            beast::error_code ec;
            ws->text(ws->got_text());
            ws->write(asio::buffer(msg), ec);
            if (ec) {
                cerr << "WS write error: " << ec.message() << endl;
            }
        }
    }
}


string buildFullMarketJson() {
    json root;
    
    // Orderbooks
    int nb = session.getNumOrderBooks();
    json arr = json::array();
    for(int i = 0; i < nb; i++){
        auto &ob = session.getOrderBook(i);
        auto levels = ob.GetOrderInfos();

        json bookObj;
        bookObj["contractIndex"] = i;

        // Bids
        json bidsArr = json::array();
        for (auto &b : levels.GetBids()) {
            json bObj;
            bObj["price"]    = b.price_;
            bObj["quantity"] = b.quantity_;
            bidsArr.push_back(bObj);
        }
        bookObj["bids"] = bidsArr;

        // Asks
        json asksArr = json::array();
        for (auto &a : levels.GetAsks()) {
            json aObj;
            aObj["price"]    = a.price_;
            aObj["quantity"] = a.quantity_;
            asksArr.push_back(aObj);
        }
        bookObj["asks"] = asksArr;

        arr.push_back(bookObj);
    }
    root["orderbooks"] = arr;

    json allT;
    for (auto &item : traderIDs) {
        auto &uname = item.first;
        int tid = item.second;
        allT[uname] = session.getTrader(tid).getOrdersJson();
    }
    root["allTraders"] = allT;

    json allTraderData = json::object(); 

    for (auto& [username, id] : traderIDs) {
        json traderData = session.getTrader(id).getOrdersJson();
        allTraderData[username] = traderData;
    }
    root["allTraderData"] = allTraderData; 

    return root.dump();
}


void doWebSocketSession(asio::ip::tcp::socket socket) {
    auto ws = make_shared<websocket::stream<beast::tcp_stream>>(std::move(socket));
    beast::error_code ec;

    ws->accept(ec);
    if(ec) {
        cerr << "WS accept failed: " << ec.message() << endl;
        return;
    }

    beast::flat_buffer buffer;
    ws->read(buffer, ec);
    if(ec) {
        cerr << "Failed to read username: " << ec.message() << endl;
        return;
    }

    // Parse the registration message
    string received = beast::buffers_to_string(buffer.data());
    json regMsg;
    try {
        regMsg = json::parse(received);
    } catch (const json::parse_error &e) {
        cerr << "JSON parse error: " << e.what() << endl;
        return;
    }

    if (regMsg.contains("type") && regMsg["type"] == "register" && regMsg.contains("username")) {
        string username = regMsg["username"];
        {
            lock_guard<mutex> lock(mapMutex);
            sessionToUserMap[ws.get()] = username;
        }
        cout << "WebSocket session registered for user: " << username << endl;
    } else {
        cerr << "Invalid registration message." << endl;
        return;
    }

    {
        lock_guard<mutex> lock(sessionMutex);
        activeSessions.push_back(ws);
    }

    {
        string fullData = buildFullMarketJson();
        ws->text(true);
        ws->write(asio::buffer(fullData), ec);
    }

    // Continuously read messages (if needed)
    while (true) {
        beast::flat_buffer readBuffer;
        ws->read(readBuffer, ec);
        if (ec == websocket::error::closed) {
            break;
        }
        if(ec) {
            cerr << "WS read error: " << ec.message() << endl;
            break;
        }
    }

    {
        lock_guard<mutex> lock(sessionMutex);
        activeSessions.erase(
            remove_if(activeSessions.begin(), activeSessions.end(),
                [&](const shared_ptr<websocket::stream<beast::tcp_stream>> &s) { return s.get() == ws.get(); }),
            activeSessions.end()
        );
    }
    {
        lock_guard<mutex> lock(mapMutex);
        sessionToUserMap.erase(ws.get());
    }
}

void doWebSocketSession(boost::asio::ip::tcp::socket socket);

void doAccept() {
    g_acceptor->async_accept(
        [/* capture by copy if needed */](boost::system::error_code ec, boost::asio::ip::tcp::socket socket)
        {
            if(!ec) {
                std::thread(&doWebSocketSession, std::move(socket)).detach();
            } else {
                // can log errors
            }
            doAccept();
        }
    );
}

void startWebSocketServer(unsigned short port)
{
    try {
        // listening on port 9090
        g_acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(
            g_ioc,
            boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)
        );

        std::cout << "WebSocket server listening on port " << port << std::endl;

        doAccept();

        g_ioc.run();
    }
    catch (const std::exception &e) {
        std::cerr << "WS server error: " << e.what() << std::endl;
    }
}

int main() {

    // Completed trades is a json used to calculate pnl at the end of the session
    // erasing to start
    ofstream completedFile("completedTrades.json", ofstream::out | ofstream::trunc);
    completedFile.close();

    thread wsThread([]{
        startWebSocketServer(9090);
    });
    wsThread.detach();

    httplib::Server svr;
    svr.set_mount_point("/static", "./static"); // to serve style.css

    svr.Get("/", [&](const Request& req, Response& res) {
        ifstream file("templates/home.html");
        if(file) {
            stringstream buffer;
            buffer << file.rdbuf();
            res.set_content(buffer.str(), "text/html");
        } else {
            res.set_content("Error: could not open home.html", "text/plain");
        }
    });

    // Add user
    svr.Post("/submit", [&](const Request& req, Response& res) {
        auto username = req.get_param_value("username");
        if (username.empty()) {
            res.set_content("Error: Username cannot be empty", "text/plain");
            return;
        }
        if (traderIDs.find(username) == traderIDs.end()) {
            // new user
            Trader newTrader(username, 0, vector<OrderPointer>());
            session.addTrader(newTrader);
            traderIDs[username] = newTrader.getId();
        }

        int currTraderID = traderIDs[username];

        // Render tradeFloor
        inja::Environment env;
        auto tmpl = env.parse_template("templates/tradeFloor.html");

        json data;
        data["username"] = username;
        data["id"]       = to_string(currTraderID);
        data["tradeDataJson"] = session.getTrader(currTraderID).getOrdersJson();

        string rendered = env.render(tmpl, data);
        res.set_content(rendered, "text/html");
    });

    // Place order route
    svr.Post("/tradeFloor", [&](const Request& req, Response& res) {
        string username = req.get_param_value("username");
        if(username.empty() || traderIDs.find(username) == traderIDs.end()) {
            res.set_content(R"({"error":"Invalid user"})", "application/json");
            return;
        }

        int currTraderID = traderIDs[username];
        if (req.get_param_value("isSubmitted") == "true") {
            try {
                int contract  = stoi(req.get_param_value("contract")) - 1;
                string oType  = req.get_param_value("order-type");
                string oSide  = req.get_param_value("order-side");
                double price  = stod(req.get_param_value("price"));
                int quantity  = stoi(req.get_param_value("quantity"));
                session.placeOrder(contract, oType, oSide, price, quantity, currTraderID);

            } catch (const exception &e) {
                cerr << "Error placing order: " << e.what() << endl;
                res.set_content(R"({"error":"Failed to place order"})", "application/json");
            }
        } else {
            res.set_content(R"({"message":"No order submitted"})", "application/json");
        }

        inja::Environment env;
        auto tmpl = env.parse_template("templates/tradeFloor.html");
        json data;
        data["username"] = username;
        data["id"] = currTraderID;

        string updatedJson = buildFullMarketJson();
        broadcastMessage(updatedJson);      

        string rendered = env.render(tmpl, data);
        res.set_content(rendered, "text/html");

    });


        svr.Post("/calculatePnl", [&](const Request& req, Response& res) {
        session.calculateTradersPnl();

        json response;
        response["message"] = "PnL calculated and notified to all users.";

        // Broadcast PnL to each user (NEEDS FIXING)
        {
            lock_guard<mutex> lock(sessionMutex);
            lock_guard<mutex> lockMap(mapMutex);
            for (auto &ws : activeSessions) {
                if (ws && ws->is_open()) {
                    // Get username for this session
                    string username = sessionToUserMap[ws.get()];
                    // Get PnL for this user
                    double pnl = session.getTrader(traderIDs[username]).getPnl();

                    json pnlMsg;
                    pnlMsg["type"] = "pnl";
                    pnlMsg["pnl"] = pnl;

                    beast::error_code ec;
                    ws->text(true);
                    ws->write(asio::buffer(pnlMsg.dump()), ec);
                    if(ec) {
                        cerr << "Failed to send PnL to " << username << ": " << ec.message() << endl;
                    }
                }
            }
        }

        res.set_content(response.dump(), "application/json");
    });


    svr.Get("/admin", [&](const Request& req, Response& res) {
        ifstream file("templates/admin.html");
        if(file) {
            inja::Environment env;
            auto tmpl = env.parse_template("templates/admin.html");

            json data;
            data["traders"] = json::array();
            for (auto& [username, id] : traderIDs) {
                data["traders"].push_back(username + " (ID: " + to_string(id) + ")");
            }

            string rendered = env.render(tmpl, data);
            res.set_content(rendered, "text/html");
        } else {
            res.set_content("Error: could not open admin.html", "text/plain");
        }
    });

    std::cout << "HTTP server started at http://localhost:8080" << std::endl;
    svr.listen("0.0.0.0", 8080);

    return 0;
}

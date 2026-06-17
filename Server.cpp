#include "../Shared/Common.hpp"  
#include <SFML/Network.hpp>
#include <iostream>
#include <memory>
#include <queue>
#include <ctime>
#include <string>
struct Player {
    sf::TcpSocket socket; // сокет для связи с клиентом
    int id;                                   
    std::vector<std::vector<bool>> grid; // сетка кораблей игрока 
};
// Проверка, потоплен ли корабль
bool isSunk(const std::vector<std::vector<bool>>& ship,
    const std::vector<std::vector<bool>>& hit, int r, int c)
{
    if (!hit[r][c]) return false;             // если клетка не подбита – не потоплен
    bool vis[10][10] = {};                    // посещённые клетки
    std::queue<std::pair<int, int>> q;
    q.push({ r, c });
    vis[r][c] = true;
    while (!q.empty()) {
        auto [cr, cc] = q.front(); q.pop();
        // Если нашли клетку с кораблём, но ещё не подбитую, значит корабль жив
        if (ship[cr][cc] && !hit[cr][cc]) return false;
        int dr[] = { -1, 1, 0, 0 };
        int dc[] = { 0, 0, -1, 1 };
        for (int d = 0; d < 4; ++d) {
            int nr = cr + dr[d];
            int nc = cc + dc[d];
            if (nr >= 0 && nr < 10 && nc >= 0 && nc < 10 &&
                !vis[nr][nc] && (ship[nr][nc] || hit[nr][nc]))
            {
                vis[nr][nc] = true;
                q.push({ nr, nc });
            }
        }
    }
    return true;// все клетки корабля подбиты
}
// Помечает клетки вокруг потопленного корабля как промахи
// Возвращает список новых клеток, помеченных как промах
std::vector<std::pair<int, int>> markAround(
    const std::vector<std::vector<bool>>& ship,
    std::vector<std::vector<bool>>& hit,
    std::vector<std::vector<bool>>& miss,
    int r, int c)
{
    std::vector<std::pair<int, int>> cells;   // все клетки корабля
    std::vector<std::pair<int, int>> result;  // новые промахи
    bool vis[10][10] = {};
    std::queue<std::pair<int, int>> q;
    q.push({ r, c });
    vis[r][c] = true;
    while (!q.empty()) {
        auto [cr, cc] = q.front(); q.pop();
        cells.push_back({ cr, cc });
        int dr[] = { -1, 1, 0, 0 };
        int dc[] = { 0, 0, -1, 1 };
        for (int d = 0; d < 4; ++d) {
            int nr = cr + dr[d];
            int nc = cc + dc[d];
            if (nr >= 0 && nr < 10 && nc >= 0 && nc < 10 &&
                !vis[nr][nc] && (ship[nr][nc] || hit[nr][nc]))
            {
                vis[nr][nc] = true;
                q.push({ nr, nc });
            }
        }
    }
    for (auto& [sr, sc] : cells) {
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr == 0 && dc == 0) continue;
                int nr = sr + dr;
                int nc = sc + dc;
                if (nr >= 0 && nr < 10 && nc >= 0 && nc < 10 &&
                    !ship[nr][nc] && !hit[nr][nc] && !miss[nr][nc])
                {
                    miss[nr][nc] = true;
                    result.push_back({ nr, nc });
                }
            }
        }
    }
    return result;
}
int main() {
    setlocale(LC_ALL, "rus");               
    std::srand(time(nullptr));
    sf::TcpListener listener;
    listener.listen(53000); // слушаем порт 53000
    listener.setBlocking(false);// неблокирующий режим
    sf::SocketSelector sel;
    sel.add(listener);
    while (true) {
        std::vector<std::unique_ptr<Player>> players;
        // ожидание подключения двух игроков
        while (players.size() < 2) {
            if (sel.wait(sf::seconds(0.1f))) {
                if (sel.isReady(listener)) {
                    auto p = std::make_unique<Player>();
                    if (listener.accept(p->socket) == sf::Socket::Done) {
                        p->id = players.size();          // id = 0 или 1
                        p->socket.setBlocking(false);   // неблокирующий сокет клиента
                        sel.add(p->socket);             // добавляем в селектор
                        // Сообщаем клиенту его id
                        sf::Packet pkt;
                        pkt << PacketType::AssignId << p->id;
                        p->socket.send(pkt);

                        players.push_back(std::move(p));
                    }
                }
            }
        }
        //расстановка кораблей
        for (auto& pl : players)
            pl->grid.assign(10, std::vector<bool>(10, false));
        // Отправляем команду начать расстановку
        for (auto& pl : players) {
            sf::Packet p;
            p << PacketType::StartPlacement;
            pl->socket.send(p);
        }
        bool ready[2] = {};    // готовность игроков
        bool dc = false;       // флаг отключения во время расстановки
        // цикл ожидания готовности обоих игроков
        while (!(ready[0] && ready[1]) && !dc) {
            if (sel.wait(sf::seconds(0.1f))) {
                for (auto& pl : players) {
                    if (sel.isReady(pl->socket)) {
                        sf::Packet p;
                        auto st = pl->socket.receive(p);

                        if (st == sf::Socket::Done) {
                            PacketType t;
                            p >> t;
                            if (t == PacketType::PlacementDone) {
                                // Принимаем сетку от клиента 
                                for (int r = 0; r < 10; ++r)
                                    for (int c = 0; c < 10; ++c) {
                                        bool b;
                                        p >> b;
                                        pl->grid[r][c] = b;
                                    }
                                ready[pl->id] = true;   // игрок готов
                            }
                        }
                        else if (st == sf::Socket::Disconnected) {
                            dc = true;
                            // Оставшийся игрок побеждает автоматически
                            for (auto& op : players) {
                                if (op->id == 1 - pl->id) {
                                    sf::Packet out;
                                    out << PacketType::GameOver << GameResult::Win;
                                    // Пустая сетка (не важна)
                                    for (int r = 0; r < 10; ++r)
                                        for (int c = 0; c < 10; ++c)
                                            out << false;
                                    op->socket.send(out);
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
        // Если кто-то отключился, закрываем сокеты и начинаем новую игру
        if (dc) {
            for (auto& pl : players) {
                sel.remove(pl->socket);
                pl->socket.disconnect();
            }
            continue;
        }
        for (auto& pl : players) {
            sf::Packet p;
            p << PacketType::BothReady;
            pl->socket.send(p);
        }
        // Сетки попаданий и промахов для каждого игрока 
        std::vector<std::vector<bool>> hit[2], miss[2];
        for (int i = 0; i < 2; ++i) {
            hit[i].assign(10, std::vector<bool>(10, false));
            miss[i].assign(10, std::vector<bool>(10, false));
        }
        int turn = 0;   // id игрока, который ходит первым
        {
            sf::Packet p;
            p << PacketType::YourTurn;
            players[turn]->socket.send(p);
        }
        bool over = false;   // флаг завершения игры     
        // основной игровой цикл
        while (!over) {
            if (sel.wait(sf::seconds(0.1f))) {
                for (auto& pl : players) {
                    if (sel.isReady(pl->socket)) {
                        sf::Packet p;
                        auto st = pl->socket.receive(p);
                        // Обработка отключения во время боя
                        if (st != sf::Socket::Done) {
                            if (st == sf::Socket::Disconnected) {
                                int wid = 1 - pl->id;   // победитель – противник
                                for (int i = 0; i < 2; ++i) {
                                    sf::Packet out;
                                    out << PacketType::GameOver
                                        << (i == wid ? GameResult::Win : GameResult::Lose);
                                    // Заглушка сетки
                                    for (int r = 0; r < 10; ++r)
                                        for (int c = 0; c < 10; ++c)
                                            out << false;
                                    players[i]->socket.send(out);
                                }
                                over = true;
                            }
                            continue;
                        }
                        PacketType t;
                        p >> t;
                        // выстрел
                        if (t == PacketType::Shoot && pl->id == turn) {
                            int r, c;
                            p >> r >> c;
                            // ссылки на сетки противника
                            Player& opp = *players[1 - pl->id];
                            auto& sh = opp.grid;                 // его корабли
                            auto& ht = hit[1 - pl->id];          // попадания по нему
                            auto& ms = miss[1 - pl->id];         // промахи по нему
                            // Игнорируем некорректные или повторные выстрелы
                            if (r < 0 || r >= 10 || c < 0 || c >= 10 || ht[r][c] || ms[r][c])
                                continue;
                            bool hitCell = sh[r][c];
                            bool sunk = false;
                            std::vector<std::pair<int, int>> misses;
                            if (hitCell) {
                                ht[r][c] = true;
                                sunk = isSunk(sh, ht, r, c);
                                if (sunk)
                                    misses = markAround(sh, ht, ms, r, c);
                            }
                            else {
                                ms[r][c] = true;   // промах
                            }
                            // Отправляем результат выстрела обоим игрокам
                            for (int i = 0; i < 2; ++i) {
                                sf::Packet out;
                                out << PacketType::ShootResult
                                    << pl->id << r << c
                                    << hitCell << sunk;
                                players[i]->socket.send(out);
                            }
                            // Если корабль потоплен, отправляем список новых промахов вокруг
                            if (!misses.empty()) {
                                for (int i = 0; i < 2; ++i) {
                                    sf::Packet mp;
                                    mp << PacketType::MissCells
                                        << pl->id
                                        << static_cast<int>(misses.size());
                                    for (auto& [mr, mc] : misses)
                                        mp << mr << mc;
                                    players[i]->socket.send(mp);
                                }
                            }
                            // проверка остались ли у противника неподбитые корабли
                            bool noShips = true;
                            for (int rr = 0; rr < 10; ++rr)
                                for (int cc = 0; cc < 10; ++cc)
                                    if (sh[rr][cc] && !ht[rr][cc])
                                        noShips = false;
                            if (noShips) {
                                // игра закончена – отправляем GameOver с сеткой противника
                                for (int i = 0; i < 2; ++i) {
                                    sf::Packet out;
                                    out << PacketType::GameOver
                                        << (i == pl->id ? GameResult::Win : GameResult::Lose);
                                    int en = (i == 0 ? 1 : 0);
                                    for (int rr = 0; rr < 10; ++rr)
                                        for (int cc = 0; cc < 10; ++cc)
                                            out << players[en]->grid[rr][cc];
                                    players[i]->socket.send(out);
                                }
                                over = true;
                                break;
                            }
                            // если промах, ход переходит к сопернику
                            if (!hitCell)
                                turn = 1 - turn;
                            sf::Packet tp;
                            tp << PacketType::YourTurn;
                            players[turn]->socket.send(tp);
                        }
                        // Предложение ничьей 
                        else if (t == PacketType::DrawOffer) {
                            sf::Packet dp;
                            dp << PacketType::DrawOffer;
                            players[1 - pl->id]->socket.send(dp);
                        }
                        // Ответ на предложение ничьей 
                        else if (t == PacketType::DrawResponse) {
                            bool ok;
                            p >> ok;
                            if (ok) {
                                // Ничья – отправляем GameOver с Draw и сетками
                                for (int i = 0; i < 2; ++i) {
                                    sf::Packet out;
                                    out << PacketType::GameOver << GameResult::Draw;
                                    int en = (i == 0 ? 1 : 0);
                                    for (int rr = 0; rr < 10; ++rr)
                                        for (int cc = 0; cc < 10; ++cc)
                                            out << players[en]->grid[rr][cc];
                                    players[i]->socket.send(out);
                                }
                                over = true;
                            }
                            else {
                                // Отказ  уведомляем обоих
                                for (auto& pl2 : players) {
                                    sf::Packet out;
                                    out << PacketType::DrawResponse << false;
                                    pl2->socket.send(out);
                                }
                            }
                        }
                        // Сдача
                        else if (t == PacketType::Surrender) {
                            int wid = 1 - pl->id;   // победитель – противник
                            for (int i = 0; i < 2; ++i) {
                                sf::Packet out;
                                out << PacketType::GameOver
                                    << (i == wid ? GameResult::Win : GameResult::Lose);
                                int en = (i == 0 ? 1 : 0);
                                for (int rr = 0; rr < 10; ++rr)
                                    for (int cc = 0; cc < 10; ++cc)
                                        out << players[en]->grid[rr][cc];
                                players[i]->socket.send(out);
                            }
                            over = true;
                            break;
                        }
                        // Сообщение чата
                        else if (t == PacketType::ChatMessage) {
                            std::string msg;
                            p >> msg;
                            sf::Packet cp;
                            cp << PacketType::ChatMessage << msg;
                            players[1 - pl->id]->socket.send(cp);
                        }
                    }
                    if (over) break;
                }
            }
        }
        for (auto& pl : players) {
            sel.remove(pl->socket);
            pl->socket.disconnect();
        }
    }
}
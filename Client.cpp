#include "../Shared/Common.hpp"       
#include <SFML/Graphics.hpp>          
#include <SFML/Network.hpp>           
#include <iostream>                   
#include <vector>
#include <string>
//состояния клетки на игровом поле
enum CellState { Empty, Ship, Hit, Miss };
// Состояния клиента 
enum State { Connecting, Placement, Battle, GameOver, DrawProposal, Waiting };
const float CELL = 35.f;                  // размер клетки в пикселях
const float OX_OWN = 220.f, OY = 100.f;   // смещение своего поля
const float OX_EN = 610.f;                // смещение вражеского поля
// Структура игрового поля 
struct Board {
    std::vector<std::vector<CellState>> grid;   // 10x10 клеток
    float sx, sy;                               // координаты левого верхнего угла поля
    bool hide;                                  // true – скрывать корабли (вражеское поле)

    Board(float x, float y, bool h) : sx(x), sy(y), hide(h) {
        grid.assign(10, std::vector<CellState>(10, Empty));
    }
    // Проверка, можно ли разместить корабль длины len из точки (r,c) с ориентацией hor
    bool canPlace(int r, int c, int len, bool hor) {
        for (int i = 0; i < len; ++i) {
            int cr = hor ? r : r + i;   // строка
            int cc = hor ? c + i : c;   // столбец
            if (cr < 0 || cr >= 10 || cc < 0 || cc >= 10) return false;
            // Проверяем все соседние клетки (включая диагонали)
            for (int dr = -1; dr <= 1; ++dr)
                for (int dc = -1; dc <= 1; ++dc) {
                    int nr = cr + dr, nc = cc + dc;
                    if (nr >= 0 && nr < 10 && nc >= 0 && nc < 10 && grid[nr][nc] == Ship)
                        return false;   // рядом уже есть корабль
                }
        }
        return true;
    }
    // Размещение корабля (все клетки заполняются значением Ship)
    void place(int r, int c, int len, bool hor) {
        for (int i = 0; i < len; ++i) {
            int rr = hor ? r : r + i;
            int cc = hor ? c + i : c;
            grid[rr][cc] = Ship;
        }
    }
    // Удаление корабля по любой его клетке
    // Возвращает длину удалённого корабля
    int remove(int r, int c) {
        if (grid[r][c] != Ship) return 0;
        std::vector<std::pair<int, int>> q = { {r,c} };
        bool vis[10][10] = {};
        vis[r][c] = true;
        for (size_t i = 0; i < q.size(); ++i) {
            auto [cr, cc] = q[i];
            int dr[] = { -1,1,0,0 }, dc[] = { 0,0,-1,1 };
            for (int d = 0; d < 4; ++d) {
                int nr = cr + dr[d], nc = cc + dc[d];
                if (nr >= 0 && nr < 10 && nc >= 0 && nc < 10 && !vis[nr][nc] && grid[nr][nc] == Ship) {
                    vis[nr][nc] = true;
                    q.push_back({ nr, nc });
                }
            }
        }
        for (auto& [rr, cc] : q) grid[rr][cc] = Empty;
        return q.size();
    }
    // Применение результата выстрела
    void shot(int r, int c, bool hit) {
        grid[r][c] = hit ? Hit : (grid[r][c] != Ship ? Miss : Hit);
    }
    // Отметка клеток вокруг потопленного корабля как промах
    void markMiss(const std::vector<std::pair<int, int>>& v) {
        for (auto& [r, c] : v)
            if (grid[r][c] == Empty) grid[r][c] = Miss;
    }
    // Показ вражеских кораблей после игры 
    void reveal(const std::vector<std::vector<bool>>& m) {
        for (int r = 0; r < 10; ++r)
            for (int c = 0; c < 10; ++c)
                if (m[r][c] && grid[r][c] == Empty) grid[r][c] = Ship;
    }
    // Отрисовка поля
    void draw(sf::RenderWindow& w) {
        sf::RectangleShape cell(sf::Vector2f(CELL - 2, CELL - 2));
        cell.setOutlineThickness(1);
        cell.setOutlineColor(sf::Color::Black);
        for (int r = 0; r < 10; ++r)
            for (int c = 0; c < 10; ++c) {
                cell.setPosition(sx + c * CELL, sy + r * CELL);
                switch (grid[r][c]) {
                case Hit:  cell.setFillColor(sf::Color::Red); break;
                case Miss: cell.setFillColor(sf::Color(150, 150, 150)); break;
                case Ship: cell.setFillColor(hide ? sf::Color(100, 149, 237) : sf::Color::Blue); break;
                default:   cell.setFillColor(sf::Color(100, 149, 237));
                }
                w.draw(cell);
            }
    }
};
int main() {
    sf::RenderWindow win(sf::VideoMode(1100, 600), L"Морской бой");
    sf::TcpSocket sock;
    sock.setBlocking(false);
    State state = Connecting;
    int myId = -1;// мой ID 
    bool myTurn = false;
    GameResult res;// результат игры
    Board own(OX_OWN, OY, false);// моё поле 
    Board enemy(OX_EN, OY, true);// вражеское поле 
    std::vector<int> ships = { 4, 3, 3, 2, 2, 2, 1, 1, 1, 1 }; 
    bool hor = true;                 
    sf::Font font;
    font.loadFromFile("arial.ttf"); 
    //кнопоки с текстом
    auto btn = [&](float x, float y, float w, float h, sf::Color col,
        const std::wstring& txt, sf::Text& t, int sz = 24) {
            sf::RectangleShape r(sf::Vector2f(w, h));
            r.setPosition(x, y);
            r.setFillColor(col);
            t.setFont(font);
            t.setString(txt);
            t.setCharacterSize(sz);
            t.setFillColor(sf::Color::White);
            auto b = t.getLocalBounds();
            t.setOrigin(b.left + b.width / 2, b.top + b.height / 2);
            t.setPosition(x + w / 2, y + h / 2);
            return r;
        };
    // Создаём кнопки и связанные с ними тексты
    sf::Text rt, dt, st, sendt, acct, declt, playt, exitt;
    auto readyBtn = btn(OX_OWN + 100, OY + 10 * CELL + 20, 120, 40, sf::Color::Green, L"Готов", rt);
    auto drawBtn = btn(970, 470, 120, 40, sf::Color(100, 100, 200), L"Ничья", dt);
    auto surrBtn = btn(970, 520, 120, 40, sf::Color(200, 100, 100), L"Сдаться", st);
    auto sendBtn = btn(15, 540, 190, 25, sf::Color::Green, L"Отправить", sendt, 18);
    auto acceptBtn = btn(380, 300, 120, 40, sf::Color::Green, L"Принять", acct);
    auto declineBtn = btn(600, 300, 120, 40, sf::Color::Red, L"Отклонить", declt);
    auto againBtn = btn(450, 300, 200, 50, sf::Color::Green, L"Играть снова", playt);
    auto exitBtn = btn(450, 370, 200, 50, sf::Color::Red, L"Закрыть игру", exitt);
    // Фон чата
    sf::RectangleShape chatBg(sf::Vector2f(200, 400));
    chatBg.setFillColor(sf::Color(30, 30, 40));
    chatBg.setPosition(10, 100);
    // Поле ввода чата
    sf::RectangleShape inpBox(sf::Vector2f(190, 25));
    inpBox.setFillColor(sf::Color::White);
    inpBox.setPosition(15, 510);
    sf::Text chatTxt("", font, 16);
    chatTxt.setFillColor(sf::Color::Black);
    sf::RectangleShape overl(sf::Vector2f(1100, 600));
    overl.setFillColor(sf::Color(0, 0, 0, 200));
    sf::Text offerTxt(L"Соперник предлагает ничью", font, 30);
    offerTxt.setFillColor(sf::Color::Yellow);
    offerTxt.setPosition(350, 200);
    sf::Text waitTxt(L"Ожидание соперника...", font, 28);
    waitTxt.setFillColor(sf::Color::White);
    sf::Text statusTxt(L"", font, 20);            
    statusTxt.setPosition(220, 30);
    statusTxt.setFillColor(sf::Color::White);
    sf::Text goTxt("", font, 40);                     
    goTxt.setFillColor(sf::Color::Yellow);
    std::vector<std::string> chatHist;  // история чата
    std::string chatInp;// текущий ввод
    // Пытаемся подключиться к серверу
    if (sock.connect("127.0.0.1", 53000) != sf::Socket::Done)
        statusTxt.setString(L"Нет соединения с сервером");
    else
        statusTxt.setString(L"Подключение...");
    while (win.isOpen()) {
        if (state == Placement) {
            if (!ships.empty()) {
                std::wstring orient = hor ? L"гориз." : L"верт.";
                statusTxt.setString(
                    L"Разместите " + std::to_wstring(ships[0]) +
                    L"-палубный (" + orient + L")  [R – поворот, ПКМ – удалить]");
            }
            else {
                statusTxt.setString(L"Все корабли расставлены! Нажмите ГОТОВ");
            }
        }
        sf::Event e;
        while (win.pollEvent(e)) {
            if (e.type == sf::Event::Closed) win.close();
            // Ввод текста в чат 
            if ((state == Battle || state == DrawProposal) && e.type == sf::Event::TextEntered) {
                if (e.text.unicode == 8) {       
                    if (!chatInp.empty()) chatInp.pop_back();
                }
                else if (e.text.unicode == 13) {     
                    if (!chatInp.empty()) {
                        sf::Packet p;
                        p << PacketType::ChatMessage << chatInp;
                        sock.send(p);
                        chatHist.push_back("You: " + chatInp);
                        chatInp.clear();
                    }
                }
                else if (e.text.unicode >= 32 && e.text.unicode < 128)  // печатные символы
                    chatInp += e.text.unicode;
            }
            // Поворот корабля клавишей R в режиме расстановки
            if (e.type == sf::Event::KeyPressed && state == Placement && e.key.code == sf::Keyboard::R)
                hor = !hor;
            // Клики мыши
            if (e.type == sf::Event::MouseButtonPressed) {
                auto [mx, my] = sf::Mouse::getPosition(win);   // координаты мыши
                // Кнопка отправки сообщения чата
                if (sendBtn.getGlobalBounds().contains(mx, my) && !chatInp.empty()) {
                    sf::Packet p;
                    p << PacketType::ChatMessage << chatInp;
                    sock.send(p);
                    chatHist.push_back("You: " + chatInp);
                    chatInp.clear();
                }
                // Действия в бою: ничья или сдача
                if (state == Battle) {
                    if (drawBtn.getGlobalBounds().contains(mx, my)) {
                        sf::Packet p;
                        p << PacketType::DrawOffer;
                        sock.send(p);
                    }
                    else if (surrBtn.getGlobalBounds().contains(mx, my)) {
                        sf::Packet p;
                        p << PacketType::Surrender;
                        sock.send(p);
                    }
                }
                // Кнопка "Готов" после завершения расстановки
                if (state == Placement && ships.empty() && readyBtn.getGlobalBounds().contains(mx, my)) {
                    sf::Packet p;
                    p << PacketType::PlacementDone;
                    for (int r = 0; r < 10; ++r)
                        for (int c = 0; c < 10; ++c)
                            p << (own.grid[r][c] == Ship);    // отправляем свою сетку
                    sock.send(p);
                    state = Waiting;
                }
                // Диалог принятия/отклонения ничьей
                if (state == DrawProposal) {
                    bool a = acceptBtn.getGlobalBounds().contains(mx, my);
                    bool d = declineBtn.getGlobalBounds().contains(mx, my);
                    if (a || d) {
                        sf::Packet p;
                        p << PacketType::DrawResponse << a;
                        sock.send(p);
                        state = Battle;
                    }
                }
                // Меню после завершения игры
                if (state == GameOver) {
                    if (againBtn.getGlobalBounds().contains(mx, my)) {
                        // Переподключение
                        sock.disconnect();
                        state = Connecting;
                        myId = -1;
                        myTurn = false;
                        own.grid.assign(10, std::vector<CellState>(10, Empty));
                        enemy.grid.assign(10, std::vector<CellState>(10, Empty));
                        enemy.hide = true;
                        ships = { 4, 3, 3, 2, 2, 2, 1, 1, 1, 1 };
                        hor = true;
                        chatHist.clear();
                        chatInp.clear();
                        if (sock.connect("127.0.0.1", 53000) == sf::Socket::Done)
                            sock.setBlocking(false);
                    }
                    else if (exitBtn.getGlobalBounds().contains(mx, my))
                        win.close();
                }
                // Расстановка кораблей
                if (state == Placement) {
                    int c = (mx - own.sx) / CELL, r = (my - own.sy) / CELL;  // клетка поля
                    if (r >= 0 && r < 10 && c >= 0 && c < 10) {
                        if (e.mouseButton.button == sf::Mouse::Left && !ships.empty()) {
                            int len = ships[0];   // длина текущего корабля
                            if (own.canPlace(r, c, len, hor)) {
                                own.place(r, c, len, hor);
                                std::cout << "Placed successfully." << std::endl;
                                ships.erase(ships.begin());   // удаляем размещённый корабль из списка
                            }
                            else {
                                std::cout << "Cannot place there." << std::endl;
                            }
                        }
                        else if (e.mouseButton.button == sf::Mouse::Right) {
                            int rm = own.remove(r, c);
                            if (rm > 0) {
                                ships.insert(ships.begin(), rm);  // возвращаем корабль обратно в список
                                std::cout << "Removed ship of length " << rm << std::endl;
                            }
                        }
                    }
                }
                // Выстрел по вражескому полю (если мой ход)
                if (state == Battle && myTurn && e.mouseButton.button == sf::Mouse::Left) {
                    int c = (mx - enemy.sx) / CELL, r = (my - enemy.sy) / CELL;
                    if (r >= 0 && r < 10 && c >= 0 && c < 10 &&
                        enemy.grid[r][c] != Hit && enemy.grid[r][c] != Miss) {
                        sf::Packet p;
                        p << PacketType::Shoot << r << c;
                        sock.send(p);
                        myTurn = false;   // ход передан, ждём результат
                    }
                }
            }
        }
        // Приём пакетов от сервера
        sf::Packet pkt;
        while (sock.receive(pkt) == sf::Socket::Done) {
            PacketType t;
            pkt >> t;
            if (t == PacketType::AssignId) {
                pkt >> myId;
                statusTxt.setString(L"Вы игрок " + std::to_wstring(myId));
            }
            else if (t == PacketType::StartPlacement) {
                state = Placement;
                own.grid.assign(10, std::vector<CellState>(10, Empty));
                enemy.grid.assign(10, std::vector<CellState>(10, Empty));
                enemy.hide = true;
                ships = { 4, 3, 3, 2, 2, 2, 1, 1, 1, 1 };
                hor = true;
            }
            else if (t == PacketType::BothReady) {
                state = Battle;
                statusTxt.setString(L"Бой! Ждите хода.");
            }
            else if (t == PacketType::YourTurn) {
                myTurn = true;
                statusTxt.setString(L"Ваш ход!");
            }
            else if (t == PacketType::ShootResult) {
                int sid, r, c; bool hit, sunk;
                pkt >> sid >> r >> c >> hit >> sunk;
                (sid == myId ? enemy : own).shot(r, c, hit);   // обновляем соответствующее поле
                statusTxt.setString(hit ? (sunk ? L"Потоплен!" : L"Попадание!") : L"Промах!");
            }
            else if (t == PacketType::MissCells) {
                int sid, cnt; pkt >> sid >> cnt;
                std::vector<std::pair<int, int>> mc(cnt);
                for (int i = 0; i < cnt; ++i) pkt >> mc[i].first >> mc[i].second;
                (sid == myId ? enemy : own).markMiss(mc);
            }
            else if (t == PacketType::GameOver) {
                pkt >> res;
                std::vector<std::vector<bool>> mask(10, std::vector<bool>(10));
                for (int rr = 0; rr < 10; ++rr)
                    for (int cc = 0; cc < 10; ++cc) {
                        bool b; pkt >> b; mask[rr][cc] = b;
                    }
                enemy.reveal(mask);       // показываем вражеские корабли
                enemy.hide = false;
                state = GameOver;
                myTurn = false;
                goTxt.setString(res == GameResult::Win ? L"Победа!" :
                    res == GameResult::Lose ? L"Поражение" : L"Ничья");
                auto bb = goTxt.getLocalBounds();
                goTxt.setOrigin(bb.left + bb.width / 2, bb.top + bb.height / 2);
                goTxt.setPosition(550, 200);
            }
            else if (t == PacketType::DrawOffer) {
                state = DrawProposal;
                statusTxt.setString(L"Предложение ничьей");
            }
            else if (t == PacketType::DrawResponse) {
                bool ok; pkt >> ok;
                if (!ok) statusTxt.setString(L"Ничья отклонена");
                if (state == DrawProposal) state = Battle;
            }
            else if (t == PacketType::ChatMessage) {
                std::string s; pkt >> s;
                chatHist.push_back("Opponent: " + s);
            }
        }
        // Отрисовка интерфейса
        win.clear(sf::Color(40, 44, 52));   // тёмный фон
        // Чат 
        win.draw(chatBg);
        float y = 490;
        for (int i = chatHist.size() - 1, cnt = 0; i >= 0 && cnt < 20; --i, ++cnt) {
            sf::Text line(chatHist[i], font, 14);
            line.setFillColor(sf::Color::White);
            line.setPosition(15, y - (cnt + 1) * 20);
            win.draw(line);
        }
        win.draw(inpBox);
        win.draw(sendBtn);
        win.draw(sendt);
        chatTxt.setString(chatInp);
        chatTxt.setPosition(20, 513);
        win.draw(chatTxt);
        // Игровые поля
        own.draw(win);
        enemy.draw(win);
        // Кнопки
        if (state == Placement && ships.empty()) {
            win.draw(readyBtn);
            win.draw(rt);
        }
        if (state == Battle) {
            win.draw(drawBtn);
            win.draw(dt);
            win.draw(surrBtn);
            win.draw(st);
        }
        if (state == Waiting) {
            win.draw(overl);
            waitTxt.setPosition(550 - waitTxt.getLocalBounds().width / 2, 300);
            win.draw(waitTxt);
        }
        if (state == DrawProposal) {
            win.draw(overl);
            win.draw(offerTxt);
            win.draw(acceptBtn);
            win.draw(acct);
            win.draw(declineBtn);
            win.draw(declt);
        }
        if (state == GameOver) {
            win.draw(overl);
            win.draw(goTxt);
            win.draw(againBtn);
            win.draw(playt);
            win.draw(exitBtn);
            win.draw(exitt);
        }
        win.draw(statusTxt);  
        win.display();
    }
}
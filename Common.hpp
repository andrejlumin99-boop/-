#pragma once  
#include <SFML/Network.hpp> 

// Типы пакетов, которыми обмениваются клиент и сервер
enum class PacketType {
    AssignId,        //  присвоить ID 0 или 1
    StartPlacement,  // начать расстановку кораблей
    PlacementDone,   // игрок завершил расстановку и отправляет свою сетку
    BothReady,       // оба игрока готовы, начинается бой
    YourTurn,        // сейчас ваш ход
    Shoot,           // координаты выстрела
    ShootResult,     // результат выстрела попадание,промах,потопление
    MissCells,       // клетки вокруг потопленного корабля, отмеченные промахом
    GameOver,        // игра окончена с результатом и вражеской сеткой
    DrawOffer,       // предложение ничьей
    DrawResponse,    // ответ на предложение ничьей да,нет
    Surrender,       // игрок сдаётся
    ChatMessage      // текстовое сообщение чата
};


enum class GameResult {
    Win,   // Победа
    Lose,  // Поражение
    Draw   // Ничья
};

// Размер игрового поля 
constexpr int GRID_SIZE = 10;

// Перегрузка оператора << для отправки PacketType в sf::Packet
inline sf::Packet& operator<<(sf::Packet& p, PacketType t) {
    return p << static_cast<int>(t);  // Преобразуем enum в int и отправляем
}

// Перегрузка оператора >> для чтения PacketType из sf::Packet
inline sf::Packet& operator>>(sf::Packet& p, PacketType& t) {
    int i;
    p >> i;                            // Читаем int
    t = static_cast<PacketType>(i);    // Преобразуем обратно в enum
    return p;
}

// Перегрузка оператора << для отправки GameResult в sf::Packet
inline sf::Packet& operator<<(sf::Packet& p, GameResult r) {
    return p << static_cast<int>(r);   // Преобразуем enum в int и отправляем
}

// Перегрузка оператора >> для чтения GameResult из sf::Packet
inline sf::Packet& operator>>(sf::Packet& p, GameResult& r) {
    int i;
    p >> i;                            // Читаем int
    r = static_cast<GameResult>(i);    // Преобразуем обратно в enum
    return p;
}
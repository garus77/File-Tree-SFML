#include <SFML/Graphics.hpp>
#include <iostream>
#include <filesystem>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <limits>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800
#define HORIZONTAL_PADDING 10.f
#define TEXT_SIZE 20

namespace fs = std::filesystem;

struct FileNode {
    std::string name;
    std::vector<std::shared_ptr<FileNode>> children;
    float x, y;
    int leafCount;
};

// Recursively build the file tree
std::shared_ptr<FileNode> buildTree(const fs::path& path) {
    auto node = std::make_shared<FileNode>();
    node->name = path.filename().string();
    if (fs::is_directory(path)) {
        for (auto& entry : fs::directory_iterator(path)) {
            try {
                node->children.push_back(buildTree(entry.path()));
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Error: " << e.what() << '\n';
            }
        }
    }
    return node;
}

int maxDepth = 0;
// Compute leaf counts and depth
int computeLeafs(const std::shared_ptr<FileNode>& node, int depth = 0) {
    maxDepth = std::max(maxDepth, depth);
    if (node->children.empty())
        return node->leafCount = 1;
    int sum = 0;
    for (auto& c : node->children)
        sum += computeLeafs(c, depth + 1);
    return node->leafCount = sum;
}

// Assign positions with uniform slot width
void assignPositions(const std::shared_ptr<FileNode>& node,
                     int depth, int& leafIndex,
                     float slotWidth, float ySpacing) {
    node->y = depth * ySpacing;
    if (node->children.empty()) {
        node->x = (leafIndex + 0.5f) * slotWidth;
        ++leafIndex;
    } else {
        for (auto& c : node->children)
            assignPositions(c, depth + 1, leafIndex, slotWidth, ySpacing);
        float firstX = node->children.front()->x;
        float lastX  = node->children.back()->x;
        node->x = (firstX + lastX) * 0.5f;
    }
}

// Draw tree edges using worldView
void drawEdges(sf::RenderWindow& window, 
               const std::shared_ptr<FileNode>& node) {
    for (auto& c : node->children) {
        sf::VertexArray line(sf::Lines, 2);
        line[0].position = { node->x, node->y };
        line[1].position = { c->x,        c->y };
        line[0].color = sf::Color(100, 100, 100, 100);
        window.draw(line);
        drawEdges(window, c);
    }
}

// Draw labels at world positions but fixed pixel size
void drawLabels(sf::RenderWindow& window,
                const std::shared_ptr<FileNode>& node,
                const sf::Font& font,
                float invZoom) {
    sf::Text text;
    text.setFont(font);
    text.setCharacterSize(TEXT_SIZE);
    text.setString(node->name);
    text.setOutlineThickness(-1);
    text.setOutlineColor(sf::Color::Black);

    auto bounds = text.getLocalBounds();
    text.setOrigin(bounds.left + bounds.width  / 2.f,
                   bounds.top  + bounds.height / 2.f);

    text.setPosition(node->x, node->y);
    text.setScale(invZoom, invZoom);
    text.setFillColor(sf::Color::White);
    window.draw(text);

    for (auto& c : node->children)
        drawLabels(window, c, font, invZoom);
}

int main(int argc, char* argv[])
{
    // Determine root folder path from drag-and-drop or prompt
    fs::path rootPath;
    if (argc > 1) {
        rootPath = fs::absolute(argv[1]);
        std::cout << "Opening (dropped) path: " << rootPath << std::endl;
    } else {
        std::cout << "Enter root folder path: ";
        std::string input;
        std::getline(std::cin, input);
        rootPath = fs::absolute(input);
    }

    if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
        std::cerr << "Invalid path.\n";
        return 1;
    }

    std::cout << "Building tree...";
    auto root = buildTree(rootPath);
    int totalLeaves = computeLeafs(root);
    int totalLevels = maxDepth + 1;
    std::cout << "Done!" << std::endl;

    std::cout << "Draw labels? (1/0): ";
    int isDrawLabels = 0;
    std::cin >> isDrawLabels;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::cout << "Y scale: ";
    float yScale;
    std::cin >> yScale;

    // Load font (for both labels and right-click display)
    sf::Font font;
    if (!font.loadFromFile("C:/Windows/Fonts/Arial.ttf")) {
        std::cerr << "Failed to load font.\n";
        return 1;
    }
    font.setSmooth(true);

    // Measure max text width if drawing labels
    float maxTextW = 0.f;
    if (isDrawLabels) {
        std::function<void(const std::shared_ptr<FileNode>&)> measure;
        measure = [&](auto node) {
            sf::Text t(node->name, font, TEXT_SIZE);
            maxTextW = std::max(maxTextW, t.getLocalBounds().width);
            for (auto& c : node->children)
                measure(c);
        };
        measure(root);
    }

    float slotWidth = maxTextW + HORIZONTAL_PADDING;
    float ySpacing  = yScale * WINDOW_HEIGHT / float(totalLevels);
    int leafIndex = 0;
    assignPositions(root, 0, leafIndex, slotWidth, ySpacing);

    // For storing the node selected by right-click
    std::shared_ptr<FileNode> selectedNode = nullptr;

    bool isFullscreen = false;
    sf::VideoMode windowedMode(WINDOW_WIDTH, WINDOW_HEIGHT);
    const char* windowTitle = "File Tree";

    sf::RenderWindow window(windowedMode, windowTitle, sf::Style::Close);
    window.setFramerateLimit(60);

    sf::View worldView = window.getDefaultView();
    float worldWidth = slotWidth * totalLeaves;
    worldView.setCenter(worldWidth / 2.f, WINDOW_HEIGHT / 2.f);

    float currentZoom = 1.f;
    bool running = true, panning = false;
    sf::Vector2i dragStart;
    sf::Vector2f viewStart;

    while (running) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed ||
               (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape)) {
                running = false;
            }
            else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F11) {
                isFullscreen = !isFullscreen;
                if (isFullscreen) {
                    window.create(sf::VideoMode::getDesktopMode(), windowTitle, sf::Style::Fullscreen);
                } else {
                    window.create(windowedMode, windowTitle, sf::Style::Default);
                }
                window.setFramerateLimit(60);
                sf::Vector2f px = window.getDefaultView().getSize();
                worldView.setSize(px.x * currentZoom, px.y * currentZoom);
                window.setView(worldView);
            }
            else if (event.type == sf::Event::MouseWheelScrolled) {
                float factor = (event.mouseWheelScroll.delta > 0) ? 0.8f : 1.25f;
                worldView.zoom(factor);
                currentZoom *= factor;
                window.setView(worldView);
            }
            else if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
                panning = true;
                dragStart = sf::Mouse::getPosition(window);
                viewStart = worldView.getCenter();
            }
            else if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left) {
                panning = false;
            }
            else if (event.type == sf::Event::MouseMoved && panning) {
                sf::Vector2i now = sf::Mouse::getPosition(window);
                sf::Vector2f delta(
                  (dragStart.x - now.x) * worldView.getSize().x / window.getSize().x,
                  (dragStart.y - now.y) * worldView.getSize().y / window.getSize().y
                );
                worldView.setCenter(viewStart + delta);
                window.setView(worldView);
            }
            // Right-click: find nearest node
            else if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Right) {
                auto pixel = sf::Mouse::getPosition(window);
                auto worldPos = window.mapPixelToCoords(pixel);
                float minDist = std::numeric_limits<float>::max();
                std::shared_ptr<FileNode> nearest = nullptr;
                
                std::function<void(const std::shared_ptr<FileNode>&)> findNearest;
                findNearest = [&](const std::shared_ptr<FileNode>& node) {
                    float dx = node->x - worldPos.x;
                    float dy = node->y - worldPos.y;
                    float dist = dx*dx + dy*dy;
                    if (dist < minDist) {
                        minDist = dist;
                        nearest = node;
                    }
                    for (auto& c : node->children)
                        findNearest(c);
                };
                findNearest(root);
                selectedNode = nearest;
            }
        }

        window.clear(sf::Color::Black);
        window.setView(worldView);
        drawEdges(window, root);

        if (isDrawLabels) {
            drawLabels(window, root, font, currentZoom == 0 ? 1.f : currentZoom);
        } else if (selectedNode) {
            sf::Text text;
            text.setFont(font);
            text.setCharacterSize(TEXT_SIZE);
            text.setString(selectedNode->name);
            text.setOutlineThickness(-1);
            text.setOutlineColor(sf::Color::Black);

            auto bounds = text.getLocalBounds();
            text.setOrigin(bounds.left + bounds.width/2.f,
                           bounds.top  + bounds.height/2.f);
            text.setPosition(selectedNode->x, selectedNode->y);
            text.setScale(currentZoom, currentZoom);
            text.setFillColor(sf::Color::White);
            window.draw(text);
        }

        window.display();
    }

    return 0;
}

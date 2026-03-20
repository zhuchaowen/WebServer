const canvas = document.getElementById('gameCanvas');
const ctx = canvas.getContext('2d');
const scoreEl = document.getElementById('score');
const restartBtn = document.getElementById('restart-btn');

const gridSize = 20;
const tileCount = canvas.width / gridSize;
let snake, food, dx, dy, score, gameLoop, isGameOver;

function init() {
    snake = [{x: 10, y: 10}, {x: 9, y: 10}, {x: 8, y: 10}];
    dx = 1; dy = 0; score = 0; isGameOver = false;
    scoreEl.innerText = score;
    restartBtn.style.display = 'none';
    spawnFood();
    if (gameLoop) clearInterval(gameLoop);
    gameLoop = setInterval(update, 100);
}

function spawnFood() {
    food = { x: Math.floor(Math.random() * tileCount), y: Math.floor(Math.random() * tileCount) };
    if (snake.some(seg => seg.x === food.x && seg.y === food.y)) spawnFood();
}

function update() {
    if (isGameOver) return;
    const head = { x: snake[0].x + dx, y: snake[0].y + dy };

    // 撞墙或撞自己
    if (head.x < 0 || head.x >= tileCount || head.y < 0 || head.y >= tileCount ||
        snake.some(seg => seg.x === head.x && seg.y === head.y)) {
        return gameOver();
    }

    snake.unshift(head);
    if (head.x === food.x && head.y === food.y) {
        score += 10; scoreEl.innerText = score; spawnFood();
    } else {
        snake.pop();
    }
    draw();
}

function draw() {
    ctx.fillStyle = '#2c3e50';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    // 画食物
    ctx.fillStyle = '#e74c3c';
    ctx.fillRect(food.x * gridSize, food.y * gridSize, gridSize - 2, gridSize - 2);
    // 画蛇
    snake.forEach((seg, i) => {
        ctx.fillStyle = i === 0 ? '#2ecc71' : '#27ae60';
        ctx.fillRect(seg.x * gridSize + 1, seg.y * gridSize + 1, gridSize - 2, gridSize - 2);
    });
}

function gameOver() {
    isGameOver = true; clearInterval(gameLoop);
    ctx.fillStyle = "rgba(0, 0, 0, 0.6)"; ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = "white"; ctx.font = "30px Arial"; ctx.textAlign = "center";
    ctx.fillText("Game Over", canvas.width / 2, canvas.height / 2);
    restartBtn.style.display = 'inline-block';
}

document.addEventListener('keydown', (e) => {
    if (isGameOver) return;
    const key = e.key.toLowerCase();
    if ((key === 'arrowup' || key === 'w') && dy === 0) { dx = 0; dy = -1; }
    if ((key === 'arrowdown' || key === 's') && dy === 0) { dx = 0; dy = 1; }
    if ((key === 'arrowleft' || key === 'a') && dx === 0) { dx = -1; dy = 0; }
    if ((key === 'arrowright' || key === 'd') && dx === 0) { dx = 1; dy = 0; }
});

restartBtn.addEventListener('click', init);
init();
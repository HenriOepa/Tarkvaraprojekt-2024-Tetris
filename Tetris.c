#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <stdbool.h>
#include <glib-2.0/glib.h>
#include <cairo/cairo.h>
#include <pango-1.0/pango/pango.h>
#include <graphene-config.h>
#include <gtk/gtk.h>
#include <SDL2/SDL.h>
#include <gdk/gdk.h>
#include <pthread.h>
#include <sqlite3.h>

// Kõrguse ja laiuse suhe, kui suurem ühest, siis kõrgus on suurem laiusest
#define WIN_RATIO 1.5

// Mänguvälja mõõtmed
#define WIDTH 10
#define HEIGHT 20
// Mänguvälja mõõtmed 2
#define WIDTH2 15
#define HEIGHT2 25
// Mänguvälja mõõtmed 3
#define WIDTH3 20
#define HEIGHT3 30

// Edetabeli andmebaasi nimi, kuhu skoorid lisatakse
#define DATABASE "edetabel.db"

// Kui kasutatakse linux süsteem, siis kasuta termios.h
// põhinevat nupu tuvastamist
// Vastasel juhul default on windows
#ifdef __linux__ // LINUX

#include <unistd.h>

// Magamine Linux jaoks
#define GAME_SLEEP(ms) usleep(ms)

#elif defined _WIN32 // WINDOWS

#include <windows.h>

// Magamine Windows jaoks
#define GAME_SLEEP(ms) Sleep(ms / 1000)

#endif

// Mängu info, mida luua
struct game_info
{
	int grid_height;
	int grid_width;
	int level;
	int sleep_time;
};

// Andmestruktuurid mängulaua jaoks
// Põhimõtteliselt kasutatav kui maatriks
struct board_data
{
	int value;
	bool is_piece;
	char *style;
};

// Struktuurid hetke kujundi koordinaatide hoidmiseks
struct piece_coord
{
	int y;
	int x;
};

struct piece_data
{
	int ref_y;
	int ref_x;
	int piece_width;
	int piece_height;
	int num_of_elem;
	struct piece_coord *coordinates;
};

struct updateGUI
{
	struct board_data **board;
	int width;
	int height;
	int level;
};

struct ability
{
	int row;
	int column;
	char *style;
	GtkWidget *grid;
};

struct elements
{
	int ability_1_amount;
	int ability_2_amount;
};

// Stack ja mängu ruudustik, et saaks jooksvalt muuta
static GtkWidget *stack, ***game_grid_widgets[3], ***preview_grid_widgets;
static GtkWidget *game_grid[3], *preview_grid, *elements_grid;
GtkWidget *lbl_final_score;
bool key_input_ready = FALSE, pressed = FALSE;
int pressed_key, score = 0, piece_num = 0;
const char *square_styles[] = {"square1", "square2", "square3", "square4",
							   "square5", "square6", "square7", "square8"};

// Globaalsete muutujate kaitsmiseks, kui on oht, et kaks threadi kasutavad samal ajal muutujat
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
// praegune/järgmine tükk
int currentPiece[4][4], nextPiece[4][4];

// Prototüübid
static void gameChoice(GtkWidget *);
static void setWindowVisible(GtkWidget *, gpointer);
static void gameWindow(GtkWidget *, gpointer *);
int *monitorSize();
void makeDisplayGrid(GtkWidget ***, GtkWidget *, int, int, int);
void changeGridElementCss(GtkWidget *, int, int, const char *);
void *startGame(void *);
gboolean game(gpointer);
gboolean key_press(GtkEventControllerKey *, guint, guint, GdkModifierType, gpointer);
gboolean key_release(GtkEventControllerKey *, guint, guint, GdkModifierType, gpointer);
int **dynMatrix(int, int);
struct board_data **makeBoard(int, int);
struct piece_data *storeShape(int, int);
void destroyMatrix(int **, int);
void destroyBoard(struct board_data **, int);
void delCoordContainer(struct piece_data *);
bool checkCollision(struct board_data **, struct piece_data *, int, int);
int *sizeOfPiece();
void rotatePiece(struct board_data **, struct piece_data *, int, int);
int generatePiece(int, int, int);
void clearLines(struct board_data **, int, int);
void movePiece(int, struct board_data **, struct piece_data *, int, int);
bool dropPiece(struct board_data **, struct piece_data *, int, int, int);
void togglePause();
static gboolean updateBoardCallback(gpointer);
void updateBoardGUI(struct board_data **, int, int, int);
void updateBoardOutsideThread(struct board_data **, int, int, int);
void updatePreviewGUI(int);
void updatePreviewOutsideThread(int);
static gboolean updatePreviewCallback(gpointer);
void endGame();
static gboolean endGameCallback();
void endGameOutsideThread();
static void helpWindow(GtkWidget *);
void checkForDatabase();
void scoreWindow();
void getScoresFromDb(GtkWidget *);
int abilityVertical(struct board_data **, int, int, int, int);
void updateAbilityGrid(GtkWidget *, int, int, char *);
static gboolean abilityCallback(gpointer);
void updateAbilitySqOutsideThread(GtkWidget *, int, int, char *);
void addScoreToDb();
int abilityHorizontal(struct board_data **, int, int, int, int);
void updateElements(int, int);
void updateElementsGridOutsideThread(int, int);
static gboolean elementsUpdateCallback(gpointer);

// Loob dünaamilise maatriksi suurusega y*x
int **dynMatrix(int height, int width)
{
	int i = 0, j = 0;

	int **row = (int **)malloc(height * sizeof(int *));

	// Kontrollib, kas mälu eraldati
	if (row == NULL)
	{
		exit(0);
	}

	// Eralda mälu ridadele

	for (i = 0; i < height; i++)
	{
		row[i] = (int *)malloc(width * sizeof(int));

		// Kontrollib, kas mälu eraldati
		if (row[i] == NULL)
		{

			for (j = i - 1; j >= 0; j--)
			{
				free(row[j]);
			}

			free(row);

			exit(0);
		}

		for (j = 0; j < width; j++)
		{
			row[i][j] = 0;
		}
	}

	return row;
}

// Vabasta maatriksile eraldatud mälu
void destroyMatrix(int **matrix, int height)
{
	// Iga rida vabastada eraldi
	for (int i = 0; i < height; i++)
	{
		free(matrix[i]);
	}

	free(matrix);
}

// Loob mängulaua
struct board_data **makeBoard(int height, int width)
{
	int i = 0, j = 0;

	// Mälu mänguvälja jaoks
	struct board_data **board = (struct board_data **)malloc(height * sizeof(struct board_data *));

	// Kontrollib, kas mälu eraldati
	if (board == NULL)
	{
		exit(0);
	}

	// Eralda mälu mängulaua ridadele
	for (i = 0; i < height; i++)
	{
		board[i] = (struct board_data *)malloc(width * sizeof(struct board_data));

		// Kontrollib, kas mälu eraldati
		if (board[i] == NULL)
		{
			exit(0);
		}
	}

	// Algväärtustab kõik elemendid
	for (i = 0; i < height; i++)
	{
		for (j = 0; j < width; j++)
		{
			board[i][j].value = 0;
			board[i][j].style = "square0";
			board[i][j].is_piece = false;
		}
	}

	// Tagastab pointeri eraldatud mälule
	return board;
}

// Vabasta mängulaua mälu
void destroyBoard(struct board_data **board, int height)
{
	// Iga rida vabastada eraldi
	for (int i = 0; i < height; i++)
	{
		free(board[i]);
	}

	free(board);
}

// Eraldab kuju hoidmiseks mälu
struct piece_data *storeShape(int height, int width)
{
	int i = 0, j = 0, piece_width = 0, piece_height = 0, piece_num = 0;

	int *piece_size_info = sizeOfPiece();

	piece_height = piece_size_info[0];
	piece_width = piece_size_info[1];
	piece_num = piece_size_info[2];

	// 3-4 on y min/max, 5-6 on x min/max

	// Mälu koordinaatide hoidmise jaoks
	struct piece_data *coords = (struct piece_data *)malloc(sizeof(struct piece_data));

	// Kontrollib, kas mälu eraldati
	if (coords == NULL)
	{
		exit(0);
	}

	// Hoiustab kujundi maatriksi
	coords->coordinates = (struct piece_coord *)malloc(piece_num * sizeof(struct piece_coord));

	// Kontrollib, kas mälu eraldati
	if (coords->coordinates == NULL)
	{
		// Vabasta piece_data-le antud mälu
		free(coords);
		exit(0);
	}

	// Algväärtustab kõik elemendid koordinaatidega
	// Kujund hoitakse mälus
	int element = 0;
	for (i = piece_size_info[3]; i <= piece_size_info[4]; i++)
	{
		for (j = piece_size_info[5]; j <= piece_size_info[6]; j++)
		{
			if (currentPiece[i][j])
			{
				// Koordinaadid on sõltuvuses ref_x ja ref_y-ist
				// läbi nende saab õige koordinaadi, ehk on relatiivne koordinaat
				coords->coordinates[element].y = i - piece_size_info[3];
				coords->coordinates[element].x = j - piece_size_info[5];

				// Suurenda elemendi loendit
				element++;
			}
		}
	}

	// Hoiusta kujundi dimensioonid, algne rea koordinaat ja veeru koordinaat (kõige vasakum ja ülemiseim koordinaat)
	coords->piece_height = piece_height;
	coords->piece_width = piece_width;
	coords->num_of_elem = piece_num;
	coords->ref_x = width / 2 - (piece_width / 2);
	coords->ref_y = 0;

	free(piece_size_info);

	// Tagastab pointeri eraldatud mälule
	return coords;
}

// Vabastab mälu, mis anti piece_datale
void delCoordContainer(struct piece_data *piece_data)
{
	free(piece_data->coordinates);
	free(piece_data);
}

// Kontrollib kui suur on hetkel mängus olev kujund
int *sizeOfPiece()
{
	int smallest_x = 0, largest_x = 0, piece_height = 0, num_of_elem = 0, y_min, y_max;
	bool increase_height, first_iter = true;

	for (int i = 0; i < 4; i++)
	{
		increase_height = true;

		for (int j = 0; j < 4; j++)
		{
			if (currentPiece[i][j])
			{
				// Loe kokku, mitu elementi on kujundis
				num_of_elem++;

				// vajalik vaid üks kord suurendada iga rea kohta
				if (increase_height)
				{
					piece_height++;
					increase_height = false;
				}

				// Kui esimest korda siis võtta praegused j(x) väärtused suurimaks ja väikseimaks
				if (first_iter)
				{
					smallest_x = j;
					largest_x = j;
					y_min = i; // Seda pole vaja uuesti leida sest i ei käi mitu korda üle 0-3
					y_max = i;
					first_iter = false;
				}

				// Kontrolli kas smallest_x on suurem kui praegune veeru arv
				if (smallest_x > j)
				{
					smallest_x = j;
				}

				// Kontrolli kas largest_x on väiksem kui praegune veeru arv
				if (largest_x < j)
				{
					largest_x = j;
				}

				// Suurim y koordinaat
				if (y_max < i)
				{
					y_max = i;
				}
			}
		}
	}

	// Suuruste tagastamiseks
	int *size = (int *)malloc(7 * sizeof(int));

	if (size == NULL)
	{
		exit(0);
	}

	// Kõrgus
	size[0] = piece_height;

	// Laius
	size[1] = largest_x - smallest_x + 1;

	// Mitu ruutu kujundis
	size[2] = num_of_elem;

	// Väikseims y cäärtus
	size[3] = y_min;

	// Suurim y väärtus
	size[4] = y_max;

	// Väikseim x-i väärtus
	size[5] = smallest_x;

	// Suurim x-i väärtus
	size[6] = largest_x;

	// Tagastab kujundi maatriksi suuruse, ehk kui suurt maatriksi vaja hoiustamiseks
	return size;
}

// Kontrolli põrkumist mängulaua põhjaga ning teise osadega
bool checkCollision(struct board_data **board, struct piece_data *coords, int width, int height)
{
	int y = 0, x = 0;

	for (int i = 0; i < coords->num_of_elem; i++)
	{
		// Kontrollitava koordinaatide väärtused
		// y on rida, x on veerg
		y = coords->ref_y + coords->coordinates[i].y;
		x = coords->ref_x + coords->coordinates[i].x;

		// Kui x suurem laiusest, kasuta modulot
		x = x % width;

		// Mängulaua põrandaga kokkupõrge
		if (y >= height)
		{
			return true;
		}

		// Kui mängulaual on samas kohas väärtus 1, kus salvestatud
		// koordinaat, ning koht ei ole hetkel oleva kujundi osa, siis põrkub
		if (board[y][x].value && !board[y][x].is_piece)
		{
			return true;
		}
	}

	return false;
}

// Keera tükk
void rotatePiece(struct board_data **board, struct piece_data *coords, int width, int height)
{
	int i, j;

	// Hetkel kasutuses olev maatriks, ehk ei ole veel keeratud
	// Kasutada selleks, et koordinaadid saaks panna sisse, ning seejärel liigutada keeratud maatriksi
	int **normal_matrix = dynMatrix(coords->piece_height, coords->piece_width);

	// Eralda ajutiselt mälu eelnevate koordinaatide hoidmiseks keeratud kujul
	int **rotated_matrix = dynMatrix(coords->piece_width, coords->piece_height);

	// Pane kujund maatriksi hoiustatud koordinaatide abil
	for (i = 0; i < coords->num_of_elem; i++)
	{
		normal_matrix[coords->coordinates[i].y][coords->coordinates[i].x] = 1;
	}

	// Keera kujund ümber asetades selle ajutisse maatriksi
	for (i = 0; i < coords->piece_height; i++)
	{
		for (j = 0; j < coords->piece_width; j++)
		{
			// Kujundi kõrgus ja laius algavad numbrist 1, seega vaja lahutada 1
			// Peaks keerama kujundi 90 kraadi
			rotated_matrix[j][i] = normal_matrix[coords->piece_height - i - 1][j];
		}
	}

	// Loo uus ajutine konteiner koordinaatide jaoks ning keera maatriks
	struct piece_data *temp_shape = storeShape(height, width);
	temp_shape->ref_x = coords->ref_x;
	temp_shape->ref_y = coords->ref_y;
	temp_shape->piece_height = coords->piece_width;
	temp_shape->piece_width = coords->piece_height;

	// Aseta ajutise kujundi väärtuste koordinaadid ajutise kujundi konteinerisse
	int element = 0;
	for (i = 0; i < temp_shape->piece_height; i++)
	{
		for (j = 0; j < temp_shape->piece_width; j++)
		{
			if (rotated_matrix[i][j])
			{
				temp_shape->coordinates[element].y = i;
				temp_shape->coordinates[element].x = j;

				element++;
			}
		}
	}

	// Vabasta maatriksi jaoks eraldatud mälu, coords->piece_width on maatriksi ridade arv
	// keeratud maatriksi korral
	destroyMatrix(rotated_matrix, coords->piece_width);
	destroyMatrix(normal_matrix, coords->piece_height);

	// Kui põrkub kokku, siis ära tee midagi peale mälu vabastamise
	if (checkCollision(board, temp_shape, width, height))
	{
		// Vabasta ajutise konteineri jaoks eraldatud mälu
		delCoordContainer(temp_shape);
	}
	else // Kui ei põrku, siis keera koordinaatide konteineri maatriksi
	{
		int y = 0, x = 0;

		// Eemalda praegune kujund mängulaualt
		for (i = 0; i < coords->num_of_elem; i++)
		{
			// Kui maatriksis on 1, siis kustuta mängulaualt vastav koht
			y = coords->ref_y + coords->coordinates[i].y;
			x = (coords->ref_x + coords->coordinates[i].x) % width;

			board[y][x].value = 0;
			board[y][x].is_piece = false;
			board[y][x].style = "square0";
		}

		// Sea uue maatriksi väärtused ajutisest konteinerist
		for (i = 0; i < coords->num_of_elem; i++)
		{
			coords->coordinates[i].y = temp_shape->coordinates[i].y;
			coords->coordinates[i].x = temp_shape->coordinates[i].x;
		}

		// Sätesta uus suurus ja laius maatriksi jaoks
		coords->piece_height = temp_shape->piece_height;
		coords->piece_width = temp_shape->piece_width;

		// Aseta uus kujund mängulauale uue maatriksi põhjal
		for (i = 0; i < coords->piece_height; i++)
		{
			y = coords->ref_y + coords->coordinates[i].y;
			x = (coords->ref_x + coords->coordinates[i].x) % width;

			board[y][x].value = 1;
			board[y][x].is_piece = true;
			board[y][x].style = (char *)square_styles[piece_num];
		}

		// Vabasta ajutise konteineri jaoks eraldatud mälu
		delCoordContainer(temp_shape);
	}
}

// Genereerib uue tüki
int generatePiece(int randomPiece, int width, int difficulty)
{
	int i, j;
	int pieces[8][4][4] =
		{
			{{1, 1, 1, 1},
			 {0, 0, 0, 0},
			 {0, 0, 0, 0},
			 {0, 0, 0, 0}},

			{{1, 1, 1, 0},
			 {1, 0, 0, 0},
			 {0, 0, 0, 0},
			 {0, 0, 0, 0}},

			{{1, 1, 1, 0},
			 {0, 0, 1, 0},
			 {0, 0, 0, 0},
			 {0, 0, 0, 0}},

			{{1, 1, 0, 0},
			 {1, 1, 0, 0},
			 {0, 0, 0, 0},
			 {0, 0, 0, 0}},

			{{0, 1, 1, 0},
			 {1, 1, 0, 0},
			 {0, 0, 0, 0},
			 {0, 0, 0, 0}},

			{{1, 1, 0, 0},
			 {0, 1, 1, 0},
			 {0, 0, 0, 0},
			 {0, 0, 0, 0}},

			{{0, 1, 0, 0},
			 {1, 1, 1, 0},
			 {0, 0, 0, 0},
			 {0, 0, 0, 0}},

			{{0, 1, 0, 0},
			 {1, 1, 1, 0},
			 {0, 1, 0, 0},
			 {0, 0, 0, 0}}};

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			currentPiece[i][j] = pieces[randomPiece][i][j];
		}
	}

	// Genereerib järgmise tüki
	if (difficulty == 1)
	{
		randomPiece = rand() % 7;
	}
	else
	{
		randomPiece = rand() % 8;
	}

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			nextPiece[i][j] = pieces[randomPiece][i][j];
		}
	}

	updatePreviewOutsideThread(randomPiece);

	return randomPiece;
}

// Liigutab tükki
void movePiece(int direction, struct board_data **board, struct piece_data *piece, int width, int height)
{
	int i = 0, j = 0, y = 0, x = 0, original_ref_x = 0;

	// Juhul, kui vaja taastada
	original_ref_x = piece->ref_x;

	// Referents x-koordinaat kasutades liikumissuunda ja peamist ref x-i
	piece->ref_x = piece->ref_x + direction;

	// Tükk saab liikuda üle mängulaua vasaku ja parema äära
	if (piece->ref_x < 0)
	{
		piece->ref_x = width - 1;
	}

	// Kui ref_x on suurem kui laius, siis kasuta modulot
	piece->ref_x = piece->ref_x % width;

	// Kui põrkub ei tee midagi peale mälu vabastamise
	if (checkCollision(board, piece, width, height))
	{
		// Taasta algne ref_x
		piece->ref_x = original_ref_x;
	}
	else
	{
		// Kui ei põrku, siis eemalda kõigepealt kujund mängulaualt
		for (i = 0; i < piece->num_of_elem; i++)
		{
			y = piece->ref_y + piece->coordinates[i].y;
			x = (original_ref_x + piece->coordinates[i].x) % width;

			board[y][x].value = 0;
			board[y][x].is_piece = false;
			board[y][x].style = "square0";
		}

		// Sea kujund mängulauale
		for (i = 0; i < piece->num_of_elem; i++)
		{
			y = piece->ref_y + piece->coordinates[i].y;
			x = (piece->ref_x + piece->coordinates[i].x) % width;

			board[y][x].value = 1;
			board[y][x].is_piece = true;
			board[y][x].style = (char *)square_styles[piece_num];
		}
	}
}

// Langetab tüki põhja
bool dropPiece(struct board_data **board, struct piece_data *piece, int width, int height, int sleep_time)
{
	int i = 0, j = 0, y = 0, x = 0;

	// Vasakule ja paremale liikumine kukkumise ajal
	if (key_input_ready)
	{
		switch (pressed_key)
		{
		case 3:
			movePiece(-1, board, piece, width, height);
			break;
		case 4:
			movePiece(1, board, piece, width, height);
			break;
		}

		key_input_ready = false;
	}

	// Suurenda ref_y väärtust
	piece->ref_y++;

	// Põrkub
	if (checkCollision(board, piece, width, height))
	{
		piece->ref_y--;
		// Eemaldab tüki tuvastamise mängulaualt
		// Ehk õigete koordinaatidega alla liikudes toimub põrkumine
		// seega enam alla minna ei saa ja on vaja uut tükki
		for (i = 0; i < piece->num_of_elem; i++)
		{
			y = piece->ref_y + piece->coordinates[i].y;
			x = (piece->ref_x + piece->coordinates[i].x) % width;

			board[y][x].is_piece = false;
		}

		return false;
	}
	else
	{
		// Kui ei põrku, eemalda praegune kujund mängulaualt
		for (i = 0; i < piece->num_of_elem; i++)
		{
			y = piece->ref_y - 1 + piece->coordinates[i].y;
			x = (piece->ref_x + piece->coordinates[i].x) % width;

			board[y][x].value = 0;
			board[y][x].is_piece = false;
			board[y][x].style = "square0";
		}

		// Liiguta kujundit mängulaual
		for (i = 0; i < piece->num_of_elem; i++)
		{
			y = piece->ref_y + piece->coordinates[i].y;
			x = (piece->ref_x + piece->coordinates[i].x) % width;

			board[y][x].value = 1;
			board[y][x].is_piece = true;
			board[y][x].style = (char *)square_styles[piece_num];
		}
	}

	return true;

	// drawBoard(board, height, width);
}

// Eemaldab täisridu
void clearLines(struct board_data **board, int width, int height)
{
	// Kontrollib igat rida
	for (int i = height - 1; i >= 0; i--)
	{
		bool lineFilled = true;

		// Kontrolli kas rida on täis
		for (int j = 0; j < width; j++)
		{
			// Kui üks rea element on kas 0 või kujundi osa, siis rida ei ole täis
			if (board[i][j].value == 0 || board[i][j].is_piece)
			{
				lineFilled = false;
				break;
			}
		}

		// Rida on täis ning ei sisalda kujundi lippe
		if (lineFilled)
		{
			for (int k = i; k > 0; k--)
			{
				for (int j = 0; j < width; j++)
				{
					// Ülemine rida liigutatakse alla
					board[k][j].value = board[k - 1][j].value;
					board[k][j].style = board[k - 1][j].style;
				}
			}

			// Mängulaua kõige ülemine rida sätestatakse nulliks
			for (int j = 0; j < width; j++)
			{
				board[0][j].value = 0;
				board[0][j].style = "square0";
			}

			// Suurenda skoori
			score += 10;

			// Suurenda i väärtust, et sama rida uuesti üle kontrollida
			i++;
		}
	}
}

// Alusta mänguga
void *startGame(void *args)
{
	struct game_info *info = args;
	int height = info->grid_height, width = info->grid_width, difficulty = info->level, sleep_time = info->sleep_time;
	int next, piece_width = 0, piece_height = 0, thresh_vert = 20, thresh_hor = 30, ability_vert = 10, ability_hor = 10;
	bool game_over = false, drop_piece = false, quit = false;
	score = 0;

	// Mälu eraldamine mängu jaoks
	struct board_data **board = makeBoard(height, width);

	srand(time(NULL));

	next = rand() % 7;

	while (!game_over)
	{
		drop_piece = false;

		// Praegune kujundi number
		piece_num = next;

		next = generatePiece(next, width, difficulty);

		struct piece_data *p_coords = storeShape(height, width);

		if (key_input_ready)
		{
			if (pressed_key == 5)
			{
				quit = true;
				game_over = true;
				key_input_ready = false;
				delCoordContainer(p_coords);
				break;
			}
		}

		if (score >= thresh_vert)
		{
			ability_vert++;
			thresh_vert *= 2;
		}

		if (score >= thresh_hor)
		{
			ability_hor++;
			thresh_hor *= 2;
		}

		updateElementsGridOutsideThread(ability_vert, ability_hor);

		while (!checkCollision(board, p_coords, width, height) && !quit)
		{

			GAME_SLEEP(sleep_time);
			movePiece(0, board, p_coords, width, height);
			updateBoardOutsideThread(board, width, height, difficulty);

			if (drop_piece && !quit)
			{
				if (key_input_ready)
				{
					if (pressed_key == 5)
					{
						quit = true;
						game_over = true;
						key_input_ready = false;
						break;
					}
				}

				if (!dropPiece(board, p_coords, width, height, sleep_time))
				{
					break;
				}
			}
			else
			{
				if (key_input_ready)
				{
					switch (pressed_key)
					{
					case 3:
						movePiece(-1, board, p_coords, width, height);
						break;
					case 4:
						movePiece(1, board, p_coords, width, height);
						break;
					case 2:
						ability_vert = abilityVertical(board, width, height, difficulty, ability_vert);
						updateElementsGridOutsideThread(ability_vert, ability_hor);
						break;
					case 1:
						rotatePiece(board, p_coords, width, height);
						break;
					case 7:
						drop_piece = true;
						break;
					case 5:
						quit = true;
						game_over = true;
						break;
					case 6:
						ability_hor = abilityHorizontal(board, width, height, difficulty, ability_hor);
						updateElementsGridOutsideThread(ability_vert, ability_hor);
						break;
					}

					key_input_ready = false;
				}
			}
		}

		// Vabasta mälu, mis eraldati koordinaatide jaoks
		delCoordContainer(p_coords);

		clearLines(board, width, height);

		for (int j = 0; j < width; j++)
		{
			if (board[0][j].value != 0)
			{
				game_over = true;
				break;
			}
		}
	}

	// Vabasta mängu jaoks eraldatud mälu
	destroyBoard(board, height);

	// Algväärtusta GUI mängulaud. Lihtsaim viis nii teha
	board = makeBoard(height, width);
	updateBoardOutsideThread(board, width, height, difficulty);
	destroyBoard(board, height);

	// Lõpeta mäng
	endGameOutsideThread();
}

// ##########
// ##########
// ##########

int abilityVertical(struct board_data **board, int width, int height, int difficulty, int ability_count)
{
	int i, j;

	key_input_ready = false;

	if (ability_count == 0)
	{
		return ability_count;
	}
	else
	{
		int index, prev_index = 1;
		bool active = true, reverse = false;

		while (active)
		{
			if (!reverse)
			{
				for (index = 1; index < width - 1; index++)
				{

					updateAbilitySqOutsideThread(game_grid[difficulty - 1], 0, prev_index, "squarei");
					updateAbilitySqOutsideThread(game_grid[difficulty - 1], 0, index, "squaretrack");

					GAME_SLEEP(50000);

					if (key_input_ready && pressed_key == 2)
					{
						active = false;
						key_input_ready = false;
						break;
					}

					prev_index = index;
				}

				reverse = true;
			}
			else
			{
				for (index = width - 2; index > 0; index--)
				{

					updateAbilitySqOutsideThread(game_grid[difficulty - 1], 0, prev_index, "squarei");
					updateAbilitySqOutsideThread(game_grid[difficulty - 1], 0, index, "squaretrack");

					GAME_SLEEP(50000);

					if (key_input_ready && pressed_key == 2)
					{
						active = false;
						key_input_ready = false;
						break;
					}

					prev_index = index;
				}

				reverse = false;
			}
		}

		updateAbilitySqOutsideThread(game_grid[difficulty - 1], 0, index - 1, "squarei");
		updateAbilitySqOutsideThread(game_grid[difficulty - 1], 0, index, "squarei");
		updateAbilitySqOutsideThread(game_grid[difficulty - 1], 0, index + 1, "squarei");

		for (i = 0; i < height; i++)
		{

			for (j = index - 1; j < index + 2; j++)
			{

				if (!board[i][j].is_piece)
				{
					board[i][j].style = "squareab";
				}
			}
		}

		updateBoardOutsideThread(board, width, height, difficulty);
		GAME_SLEEP(500000);

		for (i = 0; i < height; i++)
		{

			for (j = index - 1; j < index + 2; j++)
			{

				if (!board[i][j].is_piece)
				{

					board[i][j].value = 0;
					board[i][j].style = "square0";
				}
			}
		}

		updateBoardOutsideThread(board, width, height, difficulty);

		return ability_count - 1;
	}
}

int abilityHorizontal(struct board_data **board, int width, int height, int difficulty, int ability_count)
{
	int i, j;

	key_input_ready = false;

	if (ability_count == 0)
	{
		return ability_count;
	}
	else
	{
		int index, prev_index = 2;
		bool active = true, reverse = false;

		while (active)
		{
			if (!reverse)
			{
				for (index = 1; index < (height - 1); index++)
				{
					updateAbilitySqOutsideThread(game_grid[difficulty - 1], prev_index + 1, width, "squarei");
					updateAbilitySqOutsideThread(game_grid[difficulty - 1], index + 1, width, "squaretrack");

					GAME_SLEEP(50000);

					if (key_input_ready && pressed_key == 6)
					{
						active = false;
						key_input_ready = false;
						break;
					}

					prev_index = index;
				}

				reverse = true;
			}
			else
			{
				for (index = height - 2; index >= 1; index--)
				{

					updateAbilitySqOutsideThread(game_grid[difficulty - 1], prev_index + 1, width, "squarei");
					updateAbilitySqOutsideThread(game_grid[difficulty - 1], index + 1, width, "squaretrack");

					GAME_SLEEP(50000);

					if (key_input_ready && pressed_key == 6)
					{
						active = false;
						key_input_ready = false;
						break;
					}

					prev_index = index;
				}

				reverse = false;
			}
		}

		for (i = 0; i <= height; i++)
		{
			updateAbilitySqOutsideThread(game_grid[difficulty - 1], i, width, "squarei");
		}

		for (i = 0; i < width; i++)
		{

			for (j = index - 1; j < index + 2; j++)
			{

				if (!board[j][i].is_piece)
				{
					board[j][i].style = "squareab";
				}
			}
		}

		updateBoardOutsideThread(board, width, height, difficulty);
		GAME_SLEEP(500000);

		for (i = 0; i < width; i++)
		{

			for (j = index - 1; j < index + 2; j++)
			{

				if (!board[j][i].is_piece)
				{

					board[j][i].value = 0;
					board[j][i].style = "square0";
				}
			}
		}

		for (i = index + 1; i > 0; i--)
		{
			if ((i - 3) < 0)
			{
				break;
			}

			for (j = 0; j < width; j++)
			{
				if (!board[i - 3][j].is_piece)
				{
					board[i][j].value = board[i - 3][j].value;
					board[i][j].style = board[i - 3][j].style;
					board[i - 3][j].value = 0;
					board[i - 3][j].style = "square0";
				}
			}
		}

		updateBoardOutsideThread(board, width, height, difficulty);

		return ability_count - 1;
	}
}

void updateElements(int vert, int hor)
{
	pthread_mutex_lock(&mutex);

	gtk_label_set_text(GTK_LABEL(gtk_grid_get_child_at(GTK_GRID(elements_grid), 2, 0)), g_strdup_printf("%d", score));
	gtk_label_set_text(GTK_LABEL(gtk_grid_get_child_at(GTK_GRID(elements_grid), 2, 1)), g_strdup_printf("%d", vert));
	gtk_label_set_text(GTK_LABEL(gtk_grid_get_child_at(GTK_GRID(elements_grid), 2, 2)), g_strdup_printf("%d", hor));

	pthread_mutex_unlock(&mutex);
}

void updateElementsGridOutsideThread(int vert, int hor)
{
	struct elements *args = g_new(struct elements, 1);

	args->ability_1_amount = vert;
	args->ability_2_amount = hor;

	g_idle_add(elementsUpdateCallback, args);
}

static gboolean elementsUpdateCallback(gpointer user_data)
{
	struct elements *args = (struct elements *)user_data;

	updateElements(args->ability_1_amount, args->ability_2_amount);

	g_free(args);

	return FALSE;
}

// ##########
// ##########
// ##########

// Mängu akna suurus, esimene väärtus kõrgus, teine laius
int *monitorSize()
{
	SDL_DisplayMode monitor_data;
	int win_width, win_heigth;

	// Kasuta SDL-i monitori suuruse saamiseks
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
	}

	if (SDL_GetCurrentDisplayMode(0, &monitor_data) != 0)
	{
		fprintf(stderr, "SDL_GetCurrentDisplayMode Error: %s\n", SDL_GetError());
		SDL_Quit();
	}

	// Akna suuruse valimine
	win_width = monitor_data.w * 0.4;
	win_heigth = monitor_data.h * 0.8;

	// Minimaalselt 1280x720
	if (win_width < 512)
	{
		win_width = 512;
	}

	if (win_heigth < 576)
	{
		win_heigth = 576;
	}

	// SDL pole enam vaja
	SDL_Quit();

	// Kui kõrgus suurem, kui 1.5 kordne laius, siis muuda kõrguse väärtust väiksemaks
	if ((win_width * WIN_RATIO) < win_heigth)
	{
		win_heigth = win_width * WIN_RATIO;
	}

	// Kui laius suurem, kui 2/3 kordne kõrgus, siis muuda laiuse väärtust väiksemaks
	if ((win_heigth * (1 / WIN_RATIO)) < win_width)
	{
		win_width = win_heigth * (1 / WIN_RATIO);
	}

	int *monitor_info = (int *)malloc(2 * sizeof(int));

	// Kontorlli, kas mälu eraldati
	if (monitor_info == NULL)
	{
		exit(0);
	}

	// Andmete tagastamine
	monitor_info[0] = win_heigth;
	monitor_info[1] = win_width;

	return monitor_info;
}

gboolean key_release(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType modifiers, gpointer user_data)
{
	pressed = FALSE;
}

gboolean key_press(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType modifiers, gpointer user_data)
{
	if (!pressed)
	{
		pressed = TRUE;

		switch (keyval)
		{
		case GDK_KEY_W:
		case GDK_KEY_w:
			key_input_ready = TRUE;
			pressed_key = 1;
			break;
		case GDK_KEY_1:
		case GDK_KEY_KP_1:
			key_input_ready = TRUE;
			pressed_key = 2;
			break;
		case GDK_KEY_A:
		case GDK_KEY_a:
			key_input_ready = TRUE;
			pressed_key = 3;
			break;
		case GDK_KEY_D:
		case GDK_KEY_d:
			key_input_ready = TRUE;
			pressed_key = 4;
			break;
		case GDK_KEY_Q:
		case GDK_KEY_q:
			key_input_ready = TRUE;
			pressed_key = 5;
			break;
		case GDK_KEY_2:
		case GDK_KEY_KP_2:
			key_input_ready = TRUE;
			pressed_key = 6;
			break;
		case GDK_KEY_space:
			key_input_ready = TRUE;
			pressed_key = 7;
			break;
		default:
			break;
		}
	}

	return TRUE;
}

static void gameChoice(GtkWidget *window)
{
	GtkWidget *choice_window;
	GtkWidget *btn_level_1, *btn_level_2, *btn_level_3, *btn_back;
	// Out of scope kui pole static, teine võimalus kasutada heap-i, et püsiks kauem elus
	static struct game_info level1, level2, level3;

	level1.grid_height = HEIGHT;
	level1.grid_width = WIDTH;
	level1.level = 1;
	level1.sleep_time = 100000;

	level2.grid_height = HEIGHT2;
	level2.grid_width = WIDTH2;
	level2.level = 2;
	level2.sleep_time = 80000;

	level3.grid_height = HEIGHT3;
	level3.grid_width = WIDTH3;
	level3.level = 3;
	level3.sleep_time = 65000;

	choice_window = gtk_grid_new();

	gtk_grid_set_row_spacing(GTK_GRID(choice_window), 25);

	btn_level_1 = gtk_button_new_with_label("Tase 1");
	g_signal_connect(btn_level_1, "clicked", G_CALLBACK(gameWindow), &level1);
	gtk_grid_attach(GTK_GRID(choice_window), btn_level_1, 0, 0, 2, 1);

	btn_level_2 = gtk_button_new_with_label("Tase 2");
	g_signal_connect(btn_level_2, "clicked", G_CALLBACK(gameWindow), &level2);
	gtk_grid_attach(GTK_GRID(choice_window), btn_level_2, 0, 1, 2, 1);

	btn_level_3 = gtk_button_new_with_label("Tase 3");
	g_signal_connect(btn_level_3, "clicked", G_CALLBACK(gameWindow), &level3);
	gtk_grid_attach(GTK_GRID(choice_window), btn_level_3, 0, 2, 2, 1);

	btn_back = gtk_button_new_with_label("Tagasi");
	g_signal_connect(btn_back, "clicked", G_CALLBACK(setWindowVisible), "main");
	gtk_grid_attach(GTK_GRID(choice_window), btn_back, 0, 3, 2, 1);

	gtk_widget_set_halign(choice_window, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(choice_window, GTK_ALIGN_CENTER);

	gtk_stack_add_named(GTK_STACK(stack), choice_window, "game");
}

// Loo ruudustik, mida võimalik värvida
void makeDisplayGrid(GtkWidget ***display_grid, GtkWidget *grid, int grid_height, int grid_width, int square_size)
{
	int i, j;

	display_grid = g_new(GtkWidget **, grid_height);

	// Anna mälu ruudustiku jaoks
	for (i = 0; i < grid_height; i++)
	{
		display_grid[i] = g_new(GtkWidget *, grid_width);

		for (j = 0; j < grid_width; j++)
		{
			// Loo drawing area, mida annab värvida CSSiga
			display_grid[i][j] = gtk_drawing_area_new();
			// Sea ruudu suurus
			gtk_widget_set_size_request(display_grid[i][j], square_size, square_size);
			// Aseta gridi -- grid - loodud element - veeru number - rea number - rea ja veeru span, ehk mitu ruutu katab element
			gtk_grid_attach(GTK_GRID(grid), display_grid[i][j], j, i, 1, 1);

			// Anna elemendile CSS klass, et oleks võimalik värvida
			gtk_widget_add_css_class(display_grid[i][j], "square0");
		}
	}
}

void changeGridElementCss(GtkWidget *grid, int row_element, int column_element, const char *css_class_name)
{
	int i = 0;
	pthread_mutex_lock(&mutex);

	GtkWidget *grid_child = gtk_grid_get_child_at(GTK_GRID(grid), column_element, row_element);
	char **css_classes = gtk_widget_get_css_classes(grid_child);

	while (css_classes[i] != NULL)
	{
		gtk_widget_remove_css_class(grid_child, css_classes[i]);
		g_free(css_classes[i]);
		i++;
	}

	g_free(css_classes);

	gtk_widget_add_css_class(grid_child, css_class_name);

	pthread_mutex_unlock(&mutex);
}

// Loo aken mängu jaoks
static void gameWindow(GtkWidget *widget, gpointer *user_data)
{
	GtkWidget *game_win_box, *upper_box;
	struct game_info *info = (struct game_info *)user_data;
	int square_size, preview_sqr_size, temp, side_offset, i, j, k;
	bool new_win = false;

	// Akna suuruse valimine
	int *window_info = monitorSize();
	int win_hei = window_info[0];
	int win_wid = window_info[1];

	// Vabasta monitorSize poolt eraldatud mälu
	free(window_info);

	GtkCssProvider *provider;
	GFile *file = g_file_new_for_path("styles.css");

	// Lae CSS jaoks vajaminevad asjad
	provider = gtk_css_provider_new();
	gtk_css_provider_load_from_file(provider, file);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	// Kontrolli, kas leht antud mängu jaoks juba eksisteerib
	// Kui ei, siis loo uus leht selle jaoks
	switch (info->level)
	{
	case 1:
		if (gtk_stack_get_child_by_name(GTK_STACK(stack), "play1") == NULL)
		{
			new_win = true;
		}
		break;
	case 2:
		if (gtk_stack_get_child_by_name(GTK_STACK(stack), "play2") == NULL)
		{
			new_win = true;
		}
		break;
	case 3:
		if (gtk_stack_get_child_by_name(GTK_STACK(stack), "play3") == NULL)
		{
			new_win = true;
		}
		break;
	}

	if (new_win)
	{

		upper_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

		// Kujundi eelvaate ruudu suurus, valitakse väikseim
		preview_sqr_size = (win_wid * 0.3) / 4;
		temp = (win_hei * 0.2) / 4;

		if (preview_sqr_size > temp)
		{
			preview_sqr_size = temp;
		}

		preview_grid = gtk_grid_new();
		// Loo Widgetite ruudustik eelvaate jaoks
		makeDisplayGrid(preview_grid_widgets, preview_grid, 4, 4, preview_sqr_size);

		// Paigutus kastis
		g_object_set(preview_grid, "margin-start", 5, "margin-top", 5, NULL);

		// Skoori näitamiseks sildid
		GtkWidget *lbl_score = gtk_label_new("0");
		GtkWidget *lbl_ab1_num = gtk_label_new("0");
		GtkWidget *lbl_ab2_num = gtk_label_new("0");
		GtkWidget *lbl_score_tag = gtk_label_new("Sinu skoor: ");
		GtkWidget *lbl_ab1_tag = gtk_label_new("[1] - reapõhine võime: ");
		GtkWidget *lbl_ab2_tag = gtk_label_new("[2] - veerupõhine võime: ");
		GtkWidget *lbl_information = gtk_label_new("Vajuta nuppu Q, et väljuda mängust");

		// Skoori, võimete paigutuse jaoks võrgustik
		elements_grid = gtk_grid_new();
		gtk_grid_set_column_homogeneous(GTK_GRID(elements_grid), TRUE);

		gtk_grid_attach(GTK_GRID(elements_grid), lbl_score_tag, 0, 0, 2, 1);
		gtk_grid_attach(GTK_GRID(elements_grid), lbl_score, 2, 0, 2, 1);
		gtk_grid_attach(GTK_GRID(elements_grid), lbl_ab1_tag, 0, 1, 2, 1);
		gtk_grid_attach(GTK_GRID(elements_grid), lbl_ab1_num, 2, 1, 2, 1);
		gtk_grid_attach(GTK_GRID(elements_grid), lbl_ab2_tag, 0, 2, 2, 1);
		gtk_grid_attach(GTK_GRID(elements_grid), lbl_ab2_num, 2, 2, 2, 1);
		gtk_grid_attach(GTK_GRID(elements_grid), lbl_information, 0, 3, 3, 1);

		// g_object_set(elements_grid, "margin-end", 5, NULL);
		gtk_box_append(GTK_BOX(upper_box), preview_grid);
		gtk_box_append(GTK_BOX(upper_box), elements_grid);

		// Loo kast mängu akna jaoks
		game_win_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

		gtk_box_append(GTK_BOX(game_win_box), upper_box);

		// Et ruut oleks ühtlane, valib väikseima suuruse ning seab selle ruudu külgede pikkuseks
		// square_size saadakse kõigepealt läbi laiuse, temp läbi kõrguse, ning seejärel valitakse väikseim suurus ning seatakse square_size väärtuseks
		square_size = (win_wid * 0.65) / info->grid_width + 1;
		temp = (win_hei * 0.75) / info->grid_height + 1;

		if (square_size > temp)
		{
			square_size = temp;
		}

		// Ruutustikud mängulaua ja eelvaate jaoks - siia lähevad GtkWidget asjad
		game_grid[info->level - 1] = gtk_grid_new();

		// Loo widgetite ruudustik mängulaua jaoks. Tee ühevõrra suuremaks, et saaks ääri kasutada
		makeDisplayGrid(game_grid_widgets[info->level - 1], game_grid[info->level - 1], info->grid_height + 1, info->grid_width + 1, square_size);

		// Tee ääred nähtamatuks
		for (i = 0; i < info->grid_width + 1; i++)
		{
			changeGridElementCss(game_grid[info->level - 1], 0, i, "squarei");
		}

		for (i = 1; i < info->grid_height + 1; i++)
		{
			changeGridElementCss(game_grid[info->level - 1], i, info->grid_width, "squarei");
		}

		// side_offset aitab panna mängulaua ruudustiku enam-vähem keskele parema ja vasaku ääre vahel
		side_offset = (win_wid * 0.35) / 2.1;
		g_object_set(game_grid[info->level - 1], "margin-start", side_offset, "margin-end", 5, NULL);
		gtk_box_append(GTK_BOX(game_win_box), game_grid[info->level - 1]);

		// Pane leht stacki ja tee see nähtavaks
		switch (info->level)
		{
		case 1:
			gtk_stack_add_named(GTK_STACK(stack), game_win_box, "play1");
			break;
		case 2:
			gtk_stack_add_named(GTK_STACK(stack), game_win_box, "play2");
			break;
		case 3:
			gtk_stack_add_named(GTK_STACK(stack), game_win_box, "play3");
			break;
		}
	}

	switch (info->level)
	{
	case 1:
		gtk_stack_set_visible_child_name(GTK_STACK(stack), "play1");
		break;
	case 2:
		gtk_stack_set_visible_child_name(GTK_STACK(stack), "play2");
		break;
	case 3:
		gtk_stack_set_visible_child_name(GTK_STACK(stack), "play3");
		break;
	}

	gtk_label_set_text(GTK_LABEL(gtk_grid_get_child_at(GTK_GRID(elements_grid), 2, 0)), "0");
	gtk_label_set_text(GTK_LABEL(gtk_grid_get_child_at(GTK_GRID(elements_grid), 2, 1)), "0");
	gtk_label_set_text(GTK_LABEL(gtk_grid_get_child_at(GTK_GRID(elements_grid), 2, 2)), "0");

	key_input_ready = FALSE;
	score = 0;

	// Tee mäng aktiivseks
	pthread_t game_thread;
	pthread_create(&game_thread, NULL, startGame, (void *)info);
}

// Uuenda rakenduse graafilist mängulauda
void updateBoardGUI(struct board_data **board, int width, int height, int level)
{
	int i, j;

	// Uuendab kõikide mängulaua nähtavate osade CSS klasse
	for (i = 0; i < height; i++)
	{
		for (j = 0; j < width; j++)
		{
			changeGridElementCss(game_grid[level - 1], i + 1, j, board[i][j].style);
		}
	}
}

// Uuenda rakenduse graafilist mängulauda
void updatePreviewGUI(int num_of_piece)
{
	int i, j;

	// Uuendab kõikide mängulaua nähtavate osade CSS klasse
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			if (nextPiece[i][j])
			{
				changeGridElementCss(preview_grid, i, j, square_styles[num_of_piece]);
			}
			else
			{
				changeGridElementCss(preview_grid, i, j, "square0");
			}
		}
	}
}

void updateAbilityGrid(GtkWidget *grid, int row, int column, char *style)
{
	changeGridElementCss(grid, row, column, style);
}

static gboolean updateBoardCallback(gpointer user_data)
{
	struct updateGUI *args = (struct updateGUI *)user_data;

	updateBoardGUI(args->board, args->width, args->height, args->level);

	destroyBoard(args->board, args->height);
	g_free(args);

	return FALSE;
}

static gboolean updatePreviewCallback(gpointer user_data)
{
	int *num_of_piece = (int *)user_data;

	updatePreviewGUI(*num_of_piece);

	g_free(user_data);

	return FALSE;
}

static gboolean endGameCallback()
{
	endGame();

	return FALSE;
}

static gboolean abilityCallback(gpointer user_data)
{
	struct ability *args = (struct ability *)user_data;

	updateAbilityGrid(args->grid, args->row, args->column, args->style);

	g_free(args);

	return FALSE;
}

void updateBoardOutsideThread(struct board_data **board, int width, int height, int level)
{
	struct updateGUI *args = g_new(struct updateGUI, 1);
	args->board = makeBoard(height, width);

	for (int i = 0; i < height; i++)
	{
		for (int j = 0; j < width; j++)
		{
			args->board[i][j].is_piece = board[i][j].is_piece;
			args->board[i][j].value = board[i][j].value;
			args->board[i][j].style = board[i][j].style;
		}
	}

	args->width = width;
	args->height = height;
	args->level = level;
	g_idle_add(updateBoardCallback, args);
}

void updateAbilitySqOutsideThread(GtkWidget *grid, int row, int column, char *style)
{
	struct ability *data = g_new(struct ability, 1);

	data->grid = grid;
	data->row = row;
	data->column = column;
	data->style = style;

	g_idle_add(abilityCallback, data);
}

void updatePreviewOutsideThread(int next_num)
{
	int *next = g_new(int, 1);
	*next = next_num;

	g_idle_add(updatePreviewCallback, next);
}

void endGameOutsideThread()
{
	g_idle_add(endGameCallback, NULL);
}

void gameOverScreen()
{
	GtkWidget *game_over_window;
	GtkWidget *btn_levels, *btn_back;

	game_over_window = gtk_grid_new();

	gtk_grid_set_row_spacing(GTK_GRID(game_over_window), 25);

	GtkWidget *lbl_final_score_tag = gtk_label_new("Mäng läbi! Sinu skoor oli: ");

	lbl_final_score = gtk_label_new(NULL);
	gtk_label_set_text(GTK_LABEL(lbl_final_score), g_strdup_printf("%d", score));

	gtk_grid_attach(GTK_GRID(game_over_window), lbl_final_score_tag, 0, 0, 3, 1);
	gtk_grid_attach(GTK_GRID(game_over_window), lbl_final_score, 3, 0, 3, 1);

	btn_levels = gtk_button_new_with_label("Uus mäng");
	g_signal_connect(btn_levels, "clicked", G_CALLBACK(setWindowVisible), "game");
	gtk_grid_attach(GTK_GRID(game_over_window), btn_levels, 0, 2, 6, 1);

	btn_back = gtk_button_new_with_label("Tagasi peamenüüsse");
	g_signal_connect(btn_back, "clicked", G_CALLBACK(setWindowVisible), "main");
	gtk_grid_attach(GTK_GRID(game_over_window), btn_back, 0, 3, 6, 1);

	gtk_widget_set_halign(game_over_window, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(game_over_window, GTK_ALIGN_CENTER);

	gtk_stack_add_named(GTK_STACK(stack), game_over_window, "game_over");
}

void endGame()
{
	gtk_label_set_text(GTK_LABEL(lbl_final_score), g_strdup_printf("%d", score));

	addScoreToDb();

	score = 0;

	gtk_stack_set_visible_child_name(GTK_STACK(stack), "game_over");
}

static void helpWindow(GtkWidget *window)
{
	GtkWidget *help_window;
	GtkWidget *btn_back, *help_text;

	help_window = gtk_box_new(GTK_ORIENTATION_VERTICAL, 50);

	help_text = gtk_label_new(" Tetris on klassikaline mäng, kus eesmärgiks on kokku panna genereeritud tükid nii, \n"
							  " et tekiksid täisread mängulaual.\n"
							  " Täisrea puhul rida eemaldatakse ning sellest ülevalpool olevad read liigutatakse alla.\n"
							  " Mäng lõppeb, kui on jõutud mängulaua ülemisse ossa,\n"
							  " ning uut tükki ei ole võimalik lisada.\n"
							  "\n Mängus kasutatavad nupud on:\n"
							  "		A - liiguta kujundit vasakule\n"
							  "		D - liiguta kujundit paremale\n"
							  "		W - pööra kujundit\n"
							  "		Tühik - lase tükil alla kukkuda\n"
							  "		Q - lahku praegusest mängust\n"
							  "\n Lisaks on mängus kaks võimet, mida on võimalik kasutada, kui jupp ei kuku.\n"
							  " Esimene võime suudab eemaldada kujundid kolmest mängulaua reast.\n"
							  " Teine võime suudab eemaldada kujundid kolmest mängulaua tulbast.\n"
							  " Võimeid saadakse iga teadud skoori vahemiku tagant.\n"
							  "\n Võimete aktiveerimiseks vajuta nuppu:\n"
							  "		1 - aktiveerib horisontaalse/rea eemaldamise.\n"
							  "		2 - aktiveerib vertikaalse/tulba eemaldamise.\n");

	gtk_box_append(GTK_BOX(help_window), help_text);

	btn_back = gtk_button_new_with_label("Tagasi");
	g_signal_connect(btn_back, "clicked", G_CALLBACK(setWindowVisible), "main");
	gtk_box_append(GTK_BOX(help_window), btn_back);

	gtk_widget_set_halign(btn_back, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(btn_back, GTK_ALIGN_END);

	gtk_stack_add_named(GTK_STACK(stack), help_window, "help");
}

void addScoreToDb()
{
	sqlite3 *db;
	char *err_msg = 0;
	int ret_code;

	ret_code = sqlite3_open(DATABASE, &db);

	if (ret_code != SQLITE_OK)
	{
		sqlite3_close(db);
		return;
	}

	char insert_sql[30];
	sprintf(insert_sql, "INSERT INTO edetabel (score) VALUES (%d);", score);
	ret_code = sqlite3_exec(db, insert_sql, 0, 0, &err_msg);

	if (ret_code != SQLITE_OK)
	{
		sqlite3_free(err_msg);
	}

	sqlite3_close(db);

	return;
}

void getScoresFromDb(GtkWidget *grid)
{
	sqlite3 *db;
	int row = 1;

	if (sqlite3_open(DATABASE, &db) != SQLITE_OK)
	{
		return;
	}

	sqlite3_stmt *stmt;

	if (sqlite3_prepare_v2(db, "SELECT * FROM edetabel ORDER BY score DESC LIMIT 10", -1, &stmt, NULL) != SQLITE_OK)
	{
		sqlite3_close(db);
		return;
	}

	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		int scores = sqlite3_column_int(stmt, 1);

		gtk_label_set_text(GTK_LABEL(gtk_grid_get_child_at(GTK_GRID(grid), 0, row)), g_strdup_printf("%d", row));
		gtk_label_set_text(GTK_LABEL(gtk_grid_get_child_at(GTK_GRID(grid), 1, row)), g_strdup_printf("%d", scores));

		row++;
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);
}

void scoreWindow()
{
	GtkWidget *score_window, *new_grid, *old_grid, *rank_grid_lbl, *score_grid_lbl, *back_btn;

	if (gtk_stack_get_child_by_name(GTK_STACK(stack), "scores") != NULL)
	{
		old_grid = gtk_stack_get_child_by_name(GTK_STACK(stack), "scores");
		getScoresFromDb(old_grid);
	}
	else
	{
		new_grid = gtk_grid_new();

		rank_grid_lbl = gtk_label_new("Koht");
		gtk_grid_attach(GTK_GRID(new_grid), rank_grid_lbl, 0, 0, 1, 1);

		score_grid_lbl = gtk_label_new("Skoor");
		gtk_grid_attach(GTK_GRID(new_grid), score_grid_lbl, 1, 0, 1, 1);

		for (int i = 1; i <= 10; i++)
		{
			rank_grid_lbl = gtk_label_new(NULL);
			score_grid_lbl = gtk_label_new(NULL);

			gtk_label_set_text(GTK_LABEL(rank_grid_lbl), g_strdup_printf("%d", i));
			gtk_label_set_text(GTK_LABEL(score_grid_lbl), "0");

			gtk_grid_attach(GTK_GRID(new_grid), rank_grid_lbl, 0, i, 1, 1);
			gtk_grid_attach(GTK_GRID(new_grid), score_grid_lbl, 1, i, 1, 1);
		}

		back_btn = gtk_button_new_with_label("Tagasi");
		g_signal_connect(back_btn, "clicked", G_CALLBACK(setWindowVisible), "main");
		gtk_grid_attach(GTK_GRID(new_grid), back_btn, 0, 11, 2, 1);

		gtk_grid_set_column_homogeneous(GTK_GRID(new_grid), TRUE);
		gtk_grid_set_column_spacing(GTK_GRID(new_grid), 45);
		gtk_grid_set_row_spacing(GTK_GRID(new_grid), 10);

		getScoresFromDb(new_grid);

		score_window = new_grid;
		gtk_widget_set_halign(score_window, GTK_ALIGN_CENTER);
		gtk_widget_set_valign(score_window, GTK_ALIGN_CENTER);

		gtk_stack_add_named(GTK_STACK(stack), score_window, "scores");
	}

	gtk_stack_set_visible_child_name(GTK_STACK(stack), "scores");
}

void checkForDatabase()
{
	sqlite3 *database;
	int ret_code;
	char *error_msg;

	ret_code = sqlite3_open(DATABASE, &database);

	if (ret_code != SQLITE_OK)
	{
		sqlite3_close(database);
	}

	ret_code = sqlite3_exec(database, "CREATE TABLE IF NOT EXISTS edetabel (id INTEGER PRIMARY KEY, score INTEGER);", 0, 0, &error_msg);

	if (ret_code != SQLITE_OK)
	{
		sqlite3_free(error_msg);
	}

	sqlite3_close(database);
}

// Tee aken nähtavaks stackist
static void setWindowVisible(GtkWidget *widget, gpointer user_data)
{
	const char *win_name = (const char *)user_data;
	gtk_stack_set_visible_child_name(GTK_STACK(stack), (const char *)win_name);
}

// Alusta rakendusega
static void activate(GtkApplication *app, gpointer user_data)
{
	GtkWidget *window, *help_window, *game_choice_window, *score_window;
	GtkWidget *btn, *box, *grid;
	GtkCssProvider *provider;
	GFile *file;
	GtkEventController *controller;

	// Kontroller nupuvajutuste tuvastamise jaoks
	controller = gtk_event_controller_key_new();

	// Loo aken rakendusele
	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Tetris");

	// Saa monitori suurus
	int *window_info = monitorSize();

	// Akna suuruse valimine
	int win_hei = window_info[0];
	int win_wid = window_info[1];

	// Vabasta monitorSize poolt eraldatud mälu
	free(window_info);

	// Sätesta akna suurus
	gtk_window_set_default_size(GTK_WINDOW(window), win_wid, win_hei);

	checkForDatabase();

	// Aknad programmi jaoks, kasutab stacki selle jaoks
	// Ning selle põhjal teeb aknaid nähtavaks
	stack = gtk_stack_new();
	gtk_window_set_child(GTK_WINDOW(window), stack);

	// Loo mängu valiku aken
	gameChoice(window);

	// Loo abi aken
	helpWindow(window);

	gameOverScreen();

	// Loo uus võrgustik
	grid = gtk_grid_new();

	// Nupud ja nende vajutamistel tehakse stacki "aknad" nähtavaks
	btn = gtk_button_new_with_label("Mängima");
	g_signal_connect(btn, "clicked", G_CALLBACK(setWindowVisible), "game");
	gtk_grid_attach(GTK_GRID(grid), btn, 0, 0, 2, 1);

	btn = gtk_button_new_with_label("Abi");
	g_signal_connect(btn, "clicked", G_CALLBACK(setWindowVisible), "help");
	gtk_grid_attach(GTK_GRID(grid), btn, 0, 1, 2, 1);

	btn = gtk_button_new_with_label("Edetabel");
	g_signal_connect(btn, "clicked", G_CALLBACK(scoreWindow), NULL);
	gtk_grid_attach(GTK_GRID(grid), btn, 0, 2, 2, 1);

	// Sulgeb rakenduse
	btn = gtk_button_new_with_label("Lahku");
	g_signal_connect_swapped(btn, "clicked", G_CALLBACK(gtk_window_destroy), window);
	gtk_grid_attach(GTK_GRID(grid), btn, 0, 3, 2, 1);

	// Paneb nupud keskele
	gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(grid, GTK_ALIGN_CENTER);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 25);

	// Pane peamenüü stacki ja tee nähtavaks (et oleks esimene nähtav aken)
	gtk_stack_add_named(GTK_STACK(stack), grid, "main");
	gtk_stack_set_visible_child_name(GTK_STACK(stack), "main");

	// Lae CSS jaoks vajaminevad asjad
	provider = gtk_css_provider_new();
	file = g_file_new_for_path("styles.css");
	gtk_css_provider_load_from_file(provider, file);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	// Akna tagatausta värv läbi CSS klassi
	gtk_widget_add_css_class(window, "background");

	// Akna suurust ei saa kasutaja muuta
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

	// Loo aken
	gtk_window_present(GTK_WINDOW(window));

	// Ühendus kontrolleri ning akna vahel, ehk et nupuvajutused windowis oleks tuvastatavad
	gtk_widget_add_controller(window, controller);

	// Tuvasta nuppude signaale
	g_signal_connect(controller, "key-pressed", G_CALLBACK(key_press), NULL);
	g_signal_connect(controller, "key-released", G_CALLBACK(key_release), NULL);
}

int main(int argc, char **argv)
{
	GtkApplication *app;
	int status;

	pthread_mutex_init(&mutex, NULL);
	app = gtk_application_new("org.gtk.example", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);

	return status;
}

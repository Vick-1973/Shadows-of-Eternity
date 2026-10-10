#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
  EXPLICACIÓN RÁPIDA DEL PROYECTO:
  - El juego usa memoria dinámica (malloc/calloc/free) para personajes y
    objects; es importante liberar todo al final (se hace en main).
  - Las magias se representan con punteros a función (int (*)(void*,void*,int)).
    Para invocarlas se pasa void* y se hace un cast a Character* dentro de la
    función; esto permite reutilizar el mismo prototipo para distintos
    "casters" (jugador/enemigo).
  - Se usa un d20 (1..20) con posible modificador de suerte. El valor final
    se recorta entre 1 y 20 para evitar out-of-range.
  - Hay varios atajos/"trucos" (por ejemplo: evadir aumenta DEF en +1000 y luego
    se resta 1000) que funcionan con el diseño actual pero pueden provocar
    comportamientos inesperados si las DEF iniciales son grandes.
*/

#define MAX_NAME 32
#define MAX_INV 5
#define NUM_SPELLS 6

/* Tipos de daño - se usan para elegir si una defensa reduce el daño y para
   comprobar debilidades que duplican el daño. */
typedef enum { DMG_PHYS = 0, DMG_FIRE, DMG_FROST, DMG_LIGHT, DMG_SHADOW } DamageType;

/* Clases disponibles para el jugador. Los valores empiezan en 1 para que
   concuerden con las opciones mostradas al usuario en el menú. */
typedef enum { CLASS_ASSASSIN = 1, CLASS_KNIGHT, CLASS_MAGE, CLASS_WARRIOR } ClassType;

/*
  Estructura Character
  - Representa tanto al jugador como a enemigos.
  - Algunos campos (como classType) solo son relevantes para el jugador.
  - Las magias se guardan como punteros a función: esto permite tener
    implementaciones distintas pero la misma interfaz de invocación.
*/
typedef struct {
    char name[MAX_NAME];
    int hp;             /* Vida actual */
    int max_hp;         /* Vida máxima */
    int atk;            /* Ataque físico */
    int def;            /* Defensa física */
    int matk;           /* Ataque mágico */
    int mdef;           /* Defensa mágica */
    int luck;           /* Modificador de suerte para tiradas de d20 */
    DamageType weakness;/* Debilidad principal (si el daño coincide -> crítico x2) */
    ClassType classType;/* Solo usado para el jugador; enemigos lo ignoran */

    /* Magias: número, punteros y nombres para mostrarlas */
    int numSpells;
    int (*spells[NUM_SPELLS])(void *attacker, void *defender, int d20);
    const char *spellNames[NUM_SPELLS];

    /* Mejoras temporales que duran 1 turno (multiplicadores) */
    int tempAtkMul;     /* 1 por defecto, 2 si un evento establece buff x2 */
    int tempMatkMul;    /* 1 por defecto, 2 si evento duplica magia */
    int skipTurn;       /* 1 si debe perder el siguiente turno, 0 si no */
} Character;

/* Un Item que modifica un atributo al usarse. side_attr/side_power
   son efectos secundarios que se aplican cuando el objeto se consume. */
typedef struct {
    char nombre[24];            /* Nombre del objeto */
    int atributo;               /* 1=HP, 2=ATK_FIS, 3=ATK_MAG, 4=DEF_FIS, 5=DEF_MAG */
    int poder;                  /* Poder del objeto (1-5) */
} Item;

/* Prototipos y utilidades -------------------------------------------------*/
static const char* attr_word(char attr);
static void mostrar_objeto_detallado(const Item *obj);
static void aplicar_objeto(Character *c, const Item *obj);
static void show_inventory(Item **inventory, int invCount);
static int read_choice(int max_val); /* Función para leer los valores de manera más específica */

/* d20 con modificador de suerte. Retorna entre 1 y 20. Se usa
   luck_mod para mejorar la tirada del jugador u otros personajes. */
static int roll_d20(int luck_mod) {
    int base = (rand() % 20) + 1;   /* 1..20 */
    int total = base + luck_mod;
    if (total > 20) total = 20;     /* Máximo en 20 */
    if (total < 1) total = 1;       /* Mínimo en 1 */
    return total;
}

/* Clamp para mantener valores dentro de un rango, se encarga de que los límites no se pasen del máximo y mínimo. */
static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* Prototipos de magias: cada una recibe void* para attacker/defender
   y un int con el resultado del d20. Dentro de cada función se castean a
   Character* porque todas las entidades comparten la misma estructura. */
static int spell_fireball(void *a, void *b, int d20);
static int spell_frost(void *a, void *b, int d20);
static int spell_holy_blade(void *a, void *b, int d20);
static int spell_shadow_bolt(void *a, void *b, int d20);
static int spell_arcane_storm(void *a, void *b, int d20);
static int spell_fate_die(void *a, void *b, int d20);

/* Mostrar una barra/estado de vida */
static void print_hp(const Character *c) {
    printf("%s HP: %d/%d\n", c->name, c->hp, c->max_hp);
}

/* Mostrar inventario de forma concisa */
static void show_inventory(Item **inventory, int invCount) {
    printf("Inventario actual (%d/%d):\n", invCount, MAX_INV);
    if (invCount == 0) {
        printf("  (Vacío)\n");
        return;
    }
    /* Aritmética de punteros para recorrer Item** */
    Item **itPtr = inventory; /* Puntero al inicio del array de punteros */
    for (int i=0;i<invCount;i++) {
        Item *it = *itPtr; /* Desreferencia para obtener el puntero a Item (el objeto) */
        printf(" %d) ", i+1);
        mostrar_objeto_detallado(it);
        itPtr++; /* Avanza el puntero al siguiente Item* */
    }
}

/* Mostrar estadísticas completas sin consumir turno; recibe el inventario
   para poder listar los objects. */
static void show_stats(const Character *p, Item **inventory, int invCount) {
    printf("\n==== Estado de %s ====\n", p->name);
    printf("HP %d/%d | ATK %d | DEF %d | MATK %d | MDEF %d | LUCK %d\n",
           p->hp, p->max_hp, p->atk, p->def, p->matk, p->mdef, p->luck);
    if (p->tempAtkMul>1) printf("Buff temporal: ATAQUE x%d activo este turno.\n", p->tempAtkMul);
    if (p->tempMatkMul>1) printf("Buff temporal: MAGIA x%d activo este turno.\n", p->tempMatkMul);
    if (p->skipTurn) printf("Estado: Congelado/Stun (Perdera el siguiente turno).\n");
    show_inventory(inventory, invCount); // Usa la nueva funcion
}

/*
  apply_damage
  - Calcula daño final considerando:
    * Si es físico -> restar DEF; si es mágico -> restar MDEF.
    * Aplicar multiplicadores temporales del atacante (tempAtkMul/tempMatkMul).
    * Si la debilidad del defensor coincide con el tipo de daño -> x2.
  - Modifica la vida del defensor (def->hp) y devuelve el daño causado.
*/
static int apply_damage(Character *att, Character *def, int raw, DamageType type) {
    int mitigation = (type == DMG_PHYS) ? def->def : def->mdef;
    /* Aplicar multiplicadores temporales de 1 turno */
    if (type == DMG_PHYS && att->tempAtkMul>1) raw *= att->tempAtkMul;
    if (type != DMG_PHYS && att->tempMatkMul>1) raw *= att->tempMatkMul;

    int dmg = raw - mitigation;
    if (dmg < 0) dmg = 0; /* No se regresa vida con daño negativo */

    /* Crítico por debilidad: x2 */
    if (def->weakness == type) dmg *= 2;

    def->hp -= dmg;
    if (def->hp < 0) def->hp = 0; /* Si la vida llega a 0, no se obtiene vida negativa, se muere */
    return dmg;
}

/* --------------------------- IMPLEMENTACIÓN DE MAGIAS -------------------- */
/* Cada magia castea los void* a Character* y calcula un 'raw' base de daño.
   Luego llama apply_damage que aplica mitigación y debilidades. Imprime
   además mensajes de efecto para el usuario. */
static int spell_fireball(void *a, void *b, int d20) {
    Character *att = (Character*)a; Character *def = (Character*)b; /* Casteo de los punteros (void*) a punteros de Character* */
    int raw = att->matk + 8 + (d20 >= 10 ? 3 : 0); /* d20 = 10 o más, añade +3 de daño */
    int dealt = apply_damage(att, def, raw, DMG_FIRE);
    printf("%s lanza Bola de Fuego. dado=%d -> %d de dano\n", att->name, d20, dealt);
    return dealt;
}

static int spell_frost(void *a, void *b, int d20) {
    Character *att = (Character*)a; Character *def = (Character*)b;
    int raw = att->matk + 6;
    int dealt = apply_damage(att, def, raw, DMG_FROST);
    /* Congela (debilita DEF) si d20>=15 (efecto original) */
    /* La función clamp() se usa para asegurar que la DEF no caiga por debajo de 0. */
    if (d20 >= 15) { def->def = clamp(def->def - 2, 0, 999); printf("%s se le ha reducido la DEF (-2 DEF)\n", def->name); }
    /* 30% de probabilidad de congelar (pierde el turno) */
    if ((rand() % 100) < 30) { /* Si el valor es 0, no se efectúa, pero si es 1 se toma como perder turno*/
        def->skipTurn = 1; /* Marcar para que pierda el siguiente turno */
        printf("El frio extremo de la Escarcha ha CONGELADO a %s! Pierde el proximo turno.\n", def->name);
    }
    printf("%s invoca Escarcha. dado=%d -> %d de dano\n", att->name, d20, dealt);
    return dealt;
}

static int spell_holy_blade(void *a, void *b, int d20) {
    Character *att = (Character*)a; Character *def = (Character*)b;
    int raw = att->matk + att->atk + 4; /* mezcla de físico y mágico */
    int dealt = apply_damage(att, def, raw, DMG_LIGHT);
    printf("%s usa Espada de Luz. dado=%d -> %d de dano\n", att->name, d20, dealt);
    return dealt;
}
static int spell_shadow_bolt(void *a, void *b, int d20) {
    Character *att = (Character*)a; Character *def = (Character*)b;
    int raw = att->matk + 7;
    int dealt = apply_damage(att, def, raw, DMG_SHADOW);
    printf("%s lanza Rayo Sombrio. dado=%d -> %d de dano\n", att->name, d20, dealt);
    return dealt;
}
/* Tormenta Arcana: buena base y puede debilitar la defensa magica del oponente */
static int spell_arcane_storm(void *a, void *b, int d20) {
    Character *att = (Character*)a; Character *def = (Character*)b;
    int raw = att->matk + 10 + (d20>=12?3:0); /*Alto daño base (MATK + 10) con bono de +3 por buena tirada de dado (d20 >= 12)*/
    int dealt = apply_damage(att, def, raw, DMG_LIGHT);
    /*Efecto secundario: Si d20 >= 14, reduce la MDEF mágica en 2.*/
    if (d20>=14) { def->mdef = clamp(def->mdef - 2, 0, 999); printf("La Tormenta Arcana desgasta la defensa magica de %s (-2 MDEF)\n", def->name); }
    printf("%s desata Tormenta Arcana. dado=%d -> %d de dano\n", att->name, d20, dealt);
    return dealt;
}
/* Hechizo "Dado del Destino" - efecto especial: con d20==20 mata a jefes especiales
   devuelve la cantidad de HP dañada (antes del efecto) para consistencia con
   otras magias. */
static int spell_fate_die(void *a, void *b, int d20) {
    Character *att = (Character*)a; Character *def = (Character*)b;
    if (d20 == 20) {
        int before = def->hp; /*Guarda la vida anterior para retornarla como daño*/
        def->hp = 0; /* muerte instantánea */
        printf("%s activa Dado del Destino! dado=20 -> Muerte instantanea de %s!\n", att->name, def->name);
        return before;
    }
    printf("%s activa Dado del Destino... dado=%d -> no pasa nada.\n", att->name, d20);
    return 0;
}

/* Asignación de magias al jugador según su clase. */
static void assign_spells(Character *p) {
    int (*spell_choices[NUM_SPELLS])(void*, void*, int) = { spell_fireball, spell_frost, spell_holy_blade, spell_shadow_bolt, spell_arcane_storm, spell_fate_die };
    const char *names[NUM_SPELLS] = { "Bola de Fuego", "Escarcha", "Espada de Luz", "Rayo Sombrio", "Tormenta Arcana", "Dado del Destino" };

    int need = 0;
    switch (p->classType) {
        case CLASS_ASSASSIN: need = 2; break;
        case CLASS_KNIGHT: need = 1; break;
        case CLASS_MAGE: need = 5; break; /* el mago tiene acceso a 5 de 6 hechizos */
        case CLASS_WARRIOR: default: need = 3; break;
    }
    p->numSpells = 0;
    /* Este array funciona como un conjunto de banderas  para saber qué hechizos ya han sido seleccionados, evitando duplicados */
    int used[NUM_SPELLS] = {0};
    /*La cantidad "need" son los hechizos que se necesitan para cada clase */
    while (p->numSpells < need) {
        int idx = rand() % NUM_SPELLS;
        if (!used[idx]) {
            used[idx] = 1;
            /* Asigna el puntero a función del hechizo seleccionado al array de hechizos del jugador */
            p->spells[p->numSpells] = spell_choices[idx];
            /* Asigna el puntero a función del nombre del hechizo seleccionado al array de hechizos del jugador */
            p->spellNames[p->numSpells] = names[idx];
            p->numSpells++;
        }
    }
}

/* Crear jugador: pide nombre y clase al usuario y ajusta estadísticas base. */
static Character *create_player(void) {
    Character *p = (Character*)calloc(1, sizeof(Character));
    if (!p) { fprintf(stderr, "Sin memoria\n"); exit(1); }

    printf("========================================\n");
    printf("Ingresa el nombre de tu heroe: ");
    /* fgets es segura porque lee un máximo de MAX_NAME (32) caracteres, previniendo desbordamiento de búfer. */
    fgets(p->name, MAX_NAME, stdin);
    p->name[strcspn(p->name, "\n")] = 0; /* Utiliza strcspn para encontrar la posición del primer '\n' y quitar el '\n' si lo hay */
    printf("========================================\n");

    int choice = 0;
    printf("\nElige clase:\n [1] Asesino (2 magias, mas suerte)\n [2] Caballero (1 magia, +ATK)\n [3] Mago (5 magias)\n [4] Guerrero (3 magias)\n> ");
    scanf("%d", &choice); getchar(); /* getchar para tragar el '\n' */
    if (choice < 1 || choice > 4) choice = 4; /* Condicional si se escoge un número que no esta en rango */
    p->classType = (ClassType)choice;

    /* Atributos base comunes */
    p->max_hp = 30; p->hp = p->max_hp;
    p->atk = 7; p->def = 5; p->matk = 7; p->mdef = 5; p->luck = 0;
    p->tempAtkMul = 1; p->tempMatkMul = 1; p->skipTurn = 0; /* Inicializar skipTurn a 0 */
    p->weakness = DMG_SHADOW; /* valor por defecto */

    /* Ajustes por clase */
    if (p->classType == CLASS_ASSASSIN) p->luck = 2;
    if (p->classType == CLASS_KNIGHT) p->atk += 2;
    if (p->classType == CLASS_MAGE) { p->matk += 2; p->mdef += 1; }

    assign_spells(p);
    return p;
}

/* Crear un Item con un atributo y poder dado. El nombre se compone con el
   atributo (HP/ATK/DEF/MATK/MDEF). No asigna efecto secundario aquí. */
static Item* crear_objeto_aleatorio(void) {
    Item *obj = (Item*)malloc(sizeof(Item));
    if (!obj) return NULL;
    
    const char *nombres[] = {
        "Pocion de Vida", "Espada Afilada", "Baculo Magico", "Escudo de Hierro", "Amuleto de Proteccion",
        "Elixir de Fuerza", "Gema de Poder", "Armadura Reforzada", "Collar de Resistencia", "Daga Encantada"
    };
    /* Asigna aleatoriamente el atributo que modificará el objeto. */
    obj->atributo = (rand() % 5) + 1;  /* 1-5 */
    /* Define la magnitud de mejora del objeto */
    obj->poder = (rand() % 5) + 1;     /* 1-5 */
    /* Selecciona una cadena de la lista 'nombres' y luego se copia al campo 'nombre' de la estructura Item */
    strcpy(obj->nombre, nombres[rand() % 10]);
    
    return obj;
}
/* mostrar_objeto_detallado: Muestra el nombre del objeto, el poder de su efecto y a qué atributo aplica, para que el usuario pueda tomar una decisión. */
static void mostrar_objeto_detallado(const Item *obj) {
    const char *nombres_atributos[] = {"", "Vida", "Ataque Fisico", "Ataque Magico", "Defensa Fisica", "Defensa Magica"};
    printf("  %s (+%d %s)\n", obj->nombre, obj->poder, nombres_atributos[obj->atributo]);
}

static void aplicar_objeto(Character *c, const Item *obj) {
    int antes = 0;
    const char *nombres_atributos[] = {"", "Vida", "Ataque Fisico", "Ataque Magico", "Defensa Fisica", "Defensa Magica"};
    /* Imprime el nombre del objeto, la cantidad de poder (+%d) y el nombre del atributo afectado. */
    printf("Aplicando %s...\n", obj->nombre);
    
    /* El 'switch' usa 'obj->atributo' (1-5) para determinar qué estadística del personaje debe modificarse. */
    switch (obj->atributo) {
        case 1: /* HP */
            antes = c->max_hp;
            c->max_hp = clamp(c->max_hp + obj->poder, 20, 200);
            /* Cura al personaje en la misma cantidad de la mejora de vida máxima (o hasta el nuevo máx.)*/
            c->hp = clamp(c->hp + obj->poder, 1, c->max_hp);
            printf("  %s: %d -> %d (+%d)\n", nombres_atributos[obj->atributo], antes, c->max_hp, c->max_hp - antes);
            break;
        case 2: /* Ataque Físico */
            antes = c->atk;
            c->atk = clamp(c->atk + obj->poder, 1, 200);
            printf("  %s: %d -> %d (+%d)\n", nombres_atributos[obj->atributo], antes, c->atk, c->atk - antes);
            break;
        case 3: /* Ataque Mágico */
            antes = c->matk;
            c->matk = clamp(c->matk + obj->poder, 1, 200);
            printf("  %s: %d -> %d (+%d)\n", nombres_atributos[obj->atributo], antes, c->matk, c->matk - antes);
            break;
        case 4: /* Defensa Física */
            antes = c->def;
            c->def = clamp(c->def + obj->poder, 1, 200);
            printf("  %s: %d -> %d (+%d)\n", nombres_atributos[obj->atributo], antes, c->def, c->def - antes);
            break;
        case 5: /* Defensa Mágica */
            antes = c->mdef;
            c->mdef = clamp(c->mdef + obj->poder, 1, 200);
            printf("  %s: %d -> %d (+%d)\n", nombres_atributos[obj->atributo], antes, c->mdef, c->mdef - antes);
            break;
    }
}

/* attr_word: Función auxiliar que toma un carácter que representa un atributo 
(por ejemplo: 'A' para ataque) y devuelve una cadena de texto (por ejemplo: "de ataque"). */
static const char* attr_word(char attr) {
    switch (attr) {
        case 'H': return "de HP";
        case 'A': return "de ataque";
        case 'D': return "de defensa";
        case 'M': return "de ataque magico";
        case 'G': return "de defensa magica";
        default: return "";
    }
}

/* Evento especial que mejora todas las stats por cantidades pequeñas o da
   buffs temporales. El valor del evento se decide con un d20. */
static void special_event(Character *p) {
    /* Tira un dado d20 (1-20), aplicando el modificador de Suerte (luck) del jugador. */
    int d = roll_d20(p->luck);
    printf("\n--- Evento especial (dado=%d) ---\n", d);
    if (d <= 4) {
        /* Evento 1: Curación total (d20 = 1 a 4) */
        int before = p->hp; /* Guarda la vida actual antes de curar */
        p->hp = p->max_hp; /* Restaura la vida actual (hp) al máximo (max_hp) */
        printf("Un anciano sanador te encuentra en el camino: \"Descansa, heroe\". Te curas al 100%%. (%d/%d -> %d/%d)\n", before, p->max_hp, p->hp, p->max_hp);
    } else if (d <= 8) {
        /* Evento 2: Mejora de +1 a todas las estadísticas (d20 = 5 a 8) */
        int up = 1;
        /* Guarda todos los valores antes de la mejora */
        int hpBefore = p->hp, maxBefore = p->max_hp;
        int atkB=p->atk, defB=p->def, matkB=p->matk, mdefB=p->mdef;
        /* Aplica el aumento a HP (máximo y actual), ATK, DEF, MATK, y MDEF */
        p->max_hp += up; p->hp += up; p->atk += up; p->def += up; p->matk += up; p->mdef += up;
        printf("Una hoguera inspiradora te fortalece: +%d a TODAS las estadisticas. ", up);
        printf("HP %d/%d -> %d/%d, ATK %d->%d, DEF %d->%d, MATK %d->%d, MDEF %d->%d\n",
               hpBefore, maxBefore, p->hp, p->max_hp,
               atkB, p->atk, defB, p->def, matkB, p->matk, mdefB, p->mdef);
    } else if (d <= 12) {
        /* Evento 3: Mejora de +2 a todas las estadísticas (d20 = 9 a 12) */
        int up = 2;
        /* Guarda todos los valores antes de la mejora */
        int hpBefore = p->hp, maxBefore = p->max_hp;
        int atkB=p->atk, defB=p->def, matkB=p->matk, mdefB=p->mdef;
        /* Aplica el aumento a HP (máximo y actual), ATK, DEF, MATK, y MDEF */
        p->max_hp += up; p->hp += up; p->atk += up; p->def += up; p->matk += up; p->mdef += up;
        printf("Encuentras un altar antiguo: +%d a TODO. ", up);
        printf("HP %d/%d -> %d/%d, ATK %d->%d, DEF %d->%d, MATK %d->%d, MDEF %d->%d\n",
               hpBefore, maxBefore, p->hp, p->max_hp,
               atkB, p->atk, defB, p->def, matkB, p->matk, mdefB, p->mdef);
    } else if (d <= 16) {
        /* Evento 4: Mejora de +3 a todas las estadísticas (d20 = 13 a 16) */
        int up = 3;
        /* Guarda todos los valores antes de la mejora */
        int hpBefore = p->hp, maxBefore = p->max_hp;
        int atkB=p->atk, defB=p->def, matkB=p->matk, mdefB=p->mdef;
        /* Aplica el aumento a HP (máximo y actual), ATK, DEF, MATK, y MDEF */
        p->max_hp += up; p->hp += up; p->atk += up; p->def += up; p->matk += up; p->mdef += up;
        printf("Los espiritus de los heroes te bendicen: +%d a TODO! ", up);
        printf("HP %d/%d -> %d/%d, ATK %d->%d, DEF %d->%d, MATK %d->%d, MDEF %d->%d\n",
               hpBefore, maxBefore, p->hp, p->max_hp,
               atkB, p->atk, defB, p->def, matkB, p->matk, mdefB, p->mdef);
    } else if (d <= 18) {
        /* Evento 5: Multiplicador de Ataque Físico temporal (d20 = 17 o 18) */
        /* Ataque x2 por un turno */
        p->tempAtkMul = 2;
        printf("Un guerrero legendario te entrena relampago: tu ATAQUE FISICO hace el doble en este turno!\n");
    } else {
        /* Evento 6: Multiplicador de Ataque Mágico temporal (d20 = 19 o 20) */
        /* Magia x2 por un turno - estilo Merlin */
        p->tempMatkMul = 2;
        printf("Merlin decidio bendecirte: Tu magia hace el doble de dano por un turno!\n");
    }
}

/* ----------------------- ENEMIGOS / PLANTILLAS --------------------------- */
/* Plantilla para definir enemigos con un rango de d20 asociado. EnemyTemplate[] es
   un arreglo de 5 elementos; la selección se hace buscando donde d está
   entre range_lo y range_hi. */
typedef struct { Character base; int range_lo, range_hi; } EnemyTemplate;

/*Copiaa las estadísticas base de un enemigo predefinido (la plantilla) a una nueva estructura de personaje dinámicamente creada.*/
static void fill_enemy(Character *dst, const EnemyTemplate *t) { *dst = t->base; }

/* Rellenar EnemyTemplates de enemigos (easy, mid, hard y boss). Aquí definimos los
   enemigos "estáticos" que luego se copian (fill_enemy) para crear instancias
   dinámicas en pick_enemy. */
static void build_enemy(EnemyTemplate easy[5], EnemyTemplate mid[5], EnemyTemplate hard[5], EnemyTemplate boss[5]) {
    /* El orden de campos de Character base es:
       {name, hp, max_hp, atk, def, matk, mdef, luck, weakness, classType,
       numSpells, spells[6], spellNames[6], tempAtkMul, tempMatkMul, skipTurn
       Luego los rangos de d20 para selección: range_lo, range_hi}
       */
    EnemyTemplate e1[5] = {
        {{"Bandido", 18,18, 8,4, 5,5, 0, DMG_LIGHT, 0,  0, {0}, {0},  1, 1, 0}, 1,6},
        {{"Arquero", 16,16, 7,3, 5,5, 0, DMG_FROST, 0,  0, {0}, {0},  1, 1, 0}, 7,10},
        {{"Hechicero", 14,14, 5,4, 9,5, 0, DMG_SHADOW,0,  0, {0}, {0},  1, 1, 0}, 11,14},
        {{"Soldado", 20,20, 6,7, 5,6, 0, DMG_FIRE, 0,   0, {0}, {0},  1, 1, 0}, 15,18},
        {{"Esqueleto", 22,22, 9,5, 4,4, 0, DMG_LIGHT, 0,  0, {0}, {0},  1, 1, 0}, 19,20}
    };
    EnemyTemplate m1[5] = {
        {{"Orco", 26,26, 10,6, 6,6, 0, DMG_LIGHT, 0,  0, {0}, {0},  1, 1, 0}, 1,5},
        {{"Nigromante", 22,22, 6,6, 11,7, 0, DMG_LIGHT, 0,  0, {0}, {0},  1, 1, 0}, 6,9},
        {{"Caballero Negro", 28,28, 12,8, 5,7, 0, DMG_FIRE, 0,  0, {0}, {0},  1, 1, 0}, 10,13},
        {{"Chaman", 24,24, 7,6, 10,9, 0, DMG_FROST, 0,  0, {0}, {0},  1, 1, 0}, 14,17},
        {{"Lobo Alfa", 25,25, 13,5, 4,5, 0, DMG_LIGHT, 0,  0, {0}, {0},  1, 1, 0}, 18,20}
    };
    EnemyTemplate h1[5] = {
        {{"General Orco", 32,32, 14,10, 8,9, 0, DMG_FIRE, 0,  0, {0}, {0},  1, 1, 0}, 1,6},
        {{"Archimago", 28,28, 7,8, 15,11, 0, DMG_SHADOW, 0,  0, {0}, {0},  1, 1, 0}, 7,10},
        {{"Asesino Elite", 26,26, 16,8, 7,8, 0, DMG_LIGHT, 0,  0, {0}, {0},  1, 1, 0}, 11,14},
        {{"Golem", 40,40, 10,14, 6,12, 0, DMG_FROST, 0,  0, {0}, {0},  1, 1, 0}, 15,18},
        {{"Dragon Rojo", 45,45, 18,12, 12,12, 0, DMG_LIGHT, 0,  0, {0}, {0},  1, 1, 0}, 19,20}
    };
    EnemyTemplate b1[5] = {
        {{"Wyvern Oscuro", 50,50, 18,12, 12,12, 0, DMG_LIGHT, 0,  0, {0}, {0},  1, 1, 0}, 1,5},
        {{"Senor Demonio", 55,55, 17,13, 16,14, 0, DMG_LIGHT, 0,  0, {0}, {0},  1, 1, 0}, 6,10},
        {{"Rey Esqueleto", 48,48, 15,12, 14,14, 0, DMG_LIGHT, 0,  0, {0}, {0},  1, 1, 0}, 11,13},
        {{"Liche", 52,52, 14,13, 18,16, 0, DMG_FIRE, 0,  0, {0}, {0},  1, 1, 0}, 14,15},
        {{"Vecna", 60,60, 16,16, 20,18, 0, DMG_LIGHT, 0,  0, {0}, {0},  1, 1, 0}, 16,20}
    };
    
    /* Copia e1 (easy) a easy */
    /* ptr_src es el puntero fuente, ptr_dst es el puntero destino. */
    EnemyTemplate *ptr_src = e1;
    EnemyTemplate *ptr_dst = easy;
    for (int i = 0; i < 5; i++) {
        *ptr_dst = *ptr_src; /* Copia la estructura completa (por valor) */
        ptr_src++;           /* Avanza 1 * sizeof(EnemyTemplate) */
        ptr_dst++;           /* Avanza 1 * sizeof(EnemyTemplate) */
    }

    /* Copiar m1 (mid) a mid */
    ptr_src = m1;
    ptr_dst = mid;
    for (int i = 0; i < 5; i++) {
        *ptr_dst = *ptr_src;
        ptr_src++;
        ptr_dst++;
    }
    
    /* Copiar h1 (hard) a hard */
    ptr_src = h1;
    ptr_dst = hard;
    for (int i = 0; i < 5; i++) {
        *ptr_dst = *ptr_src;
        ptr_src++;
        ptr_dst++;
    }

    /* Copiar b1 (boss) a boss */
    ptr_src = b1;
    ptr_dst = boss;
    for (int i = 0; i < 5; i++) {
        *ptr_dst = *ptr_src;
        ptr_src++;
        ptr_dst++;
    }
}

/* Elegir enemigo según el resultado d (1..20). Se crea dinámicamente
   un Character y se copia la plantilla con fill_enemy.*/
static Character *pick_enemy(const EnemyTemplate choice[5], int d) {
    for (int i=0;i<5;i++) if (d>=choice[i].range_lo && d<=choice[i].range_hi) {
        /* Asignación de memoria: Solicita memoria dinámicamente para la nueva estructura Character que representará al enemigo en la batalla. */
        Character *e = (Character*)malloc(sizeof(Character));
        /* Comprobación de error: Si malloc falla, retorna NULL */
        if (!e) return NULL;
        /* Inicialización del enemigo: Copia los datos de la plantilla estática (choice[i]) al nuevo personaje dinámico 'e' */
        fill_enemy(e, &choice[i]);
        return e;
    }
    /* Garantiza que salga un enemigo en caso de que el dado no caigo en alguno de los rangos definidos para los enemigos */
    Character *e = (Character*)malloc(sizeof(Character));
    /* Inicializa el enemigo con la primera plantilla disponible (choice[0]) como opción segura */
    fill_enemy(e, &choice[0]);
    return e;
}

/* Ataque físico simple. Si la tirada es alta (>=15) añade un bono de +5 al raw.
   Tras ejecutar el ataque resetea tempAtkMul a 1 (consumir el buff de 1 turno).
   Devuelve el daño causado. */
static int physical_attack(Character *att, Character *def, int d20) {
    int raw = att->atk + (d20 >= 15 ? 5 : 0);
    int dealt = apply_damage(att, def, raw, DMG_PHYS);
    printf("%s ataca con espada. dado=%d -> %d de dano\n", att->name, d20, dealt);
    /* Consumir mejoras temporales de 1 turno */
    att->tempAtkMul = 1;
    return dealt;
}

/* --------------------------- TURNOS ------------------------------------ */
/* Turno del jugador: muestra un menu con acciones posibles y ejecuta la
   elección. */
static void player_turn(Character *p, Character *e, Item **inventory, int *invCount) {
    printf("\n");
    print_hp(p); print_hp(e);
    int eventAvailable = (rand()%100) < 30; /* 30% de chance */
    int op = 0;
    int bonusChoice = 0; /* si el evento concede una acción extra */
    
    /* Verificación de turno saltado (CONGELADO) */
    if (p->skipTurn) {
        printf("%s esta CONGELADO y pierde su turno!\n", p->name);
        p->skipTurn = 0; /* Consume el efecto de perder turno */
        printf("========================================\n");
        return;
    }
    
    do {
        while (1) {
            printf("\nElige accion:\n [1] Ataque fisico\n [2] Usar magia (elige cual)\n [3] Evadir (50%%)\n");
            if (eventAvailable && !bonusChoice) printf(" [4] Evento especial (+stats)\n");
            if (*invCount>0) printf(" [5] Usar objeto del inventario\n");
            printf(" [6] Ver estadisticas (no consume turno)\n");
            printf("> ");
            op = read_choice(6); /* Se usa read choice */
            
            if (op == -1) { printf("Opcion invalida. Intenta de nuevo.\n"); continue; }
            if (op==6) { show_stats(p, inventory, *invCount); continue; }
            if (op==4 && (!eventAvailable || bonusChoice)) { printf("No hay evento especial disponible ahora.\n"); continue; }
            if (op==5 && *invCount<=0) { printf("No tienes objects.\n"); continue; }
            break;
        }

        int d = roll_d20(p->luck);
        printf("d20=%d\n", d);
        if (op==1) {
            physical_attack(p,e,d);
        } else if (op==2 && p->numSpells>0) {
            /* Elegir magia explícitamente */
            printf("Selecciona magia:\n");
            /* Aritmética de punteros para mostrar spells */
            const char **namePtr = p->spellNames;
            for (int i=0;i<p->numSpells;i++) {
                printf(" [%d] %s\n", i+1, *namePtr);
                namePtr++; /* Avanza al siguiente nombre de hechizo */
            }
            printf("> "); int magic_choice=1;
            magic_choice = read_choice(p->numSpells); /* Se usa read_choice*/
            if (magic_choice<1||magic_choice>p->numSpells) magic_choice=1;
            
            /* Aritmética de punteros para seleccionar el hechizo */
            /* p->spells + (magic_choice-1) es la dirección del puntero a función */
            /* *(...) desreferencia para obtener el puntero a función */
            int (*spell)(void*,void*,int) = *(p->spells + (magic_choice - 1));
            
            spell(p,e,d);
            /* Consumir mejora temporal de magia */
            p->tempMatkMul = 1;
        } else if (op==3) {
            /* Evasión: si d>=11 funciona -> aumentamos DEF temporalmente. */
            if (d>=11) printf("%s evade el siguiente ataque! (mitad de dano)\n", p->name), p->def += 1000; /* truco: DEF gigante por un turno */
            else printf("Evasion fallida.\n");
        } else if (op==4 && eventAvailable && !bonusChoice) {
            special_event(p);
            /* Después del evento permitimos elegir la acción extra (bonusChoice)
               sin consumir el turno que se otorgó. */
            bonusChoice = 1;
            continue; /* volver a mostrar el menu para elegir la acción del bono */
        } else if (op==5 && *invCount>0) {
            /* Usar objeto del inventario: mostrar y aplicar */
            show_inventory(inventory, *invCount);
            printf("Elige objeto a usar: "); int pick=1;
            pick = read_choice(*invCount); /* Se usa read_choice */
            if (pick<1||pick>*invCount) pick=1;
            
            /* Aritmética de punteros para seleccionar item y remover */
            Item *use = *(inventory + (pick-1));
            aplicar_objeto(p, use);
            printf("Has usado '%s'!\n", use->nombre);
            /* eliminar del inventario (liberar memoria) */
            free(use);
            
            /* Usa un puntero para reorganizar el inventario después de eliminar */
            Item **removePtr = inventory + (pick-1);
            for (int k=pick-1;k<*invCount-1;k++) {
                *removePtr = *(removePtr + 1); /* inventory[k] = inventory[k+1] */
                removePtr++;
            }
            
            (*invCount)--;
        }

        /* si estabamos en bonusChoice, ya se ejecuto la accion extra */
        if (bonusChoice) { bonusChoice = 0; break; }
        else break;
    } while (1);
    printf("========================================\n");
}

/* Turno de IA enemiga. Elige entre lanzar una magia (si tiene) o
   realizar un ataque físico. La probabilidad por defecto es 40% para magia. */
static void enemy_turn(Character *e, Character *p) {
    /* Verificación de turno saltado (CONGELADO) */
    if (e->skipTurn) {
        printf("%s esta CONGELADO y pierde su turno!\n", e->name);
        e->skipTurn = 0; /* Consume el efecto de perder turno */
        printf("========================================\n");
        return;
    }
    // ---------------------------------------------------
    
    int d = roll_d20(0);
    /* 60% ataque físico, 40% magia si tiene */
    int doMagic = (e->numSpells>0) && (rand()%100 < 40);
    if (doMagic) {
        int idx = rand() % e->numSpells;
        /* Aritmética de punteros para invocar hechizo */
        /* Accede al puntero a función en la posición 'idx' */
        int (*spell)(void*,void*,int) = *(e->spells + idx);
        spell(e,p,d);
        // ------------------------------------------------------------
    } else {
        physical_attack(e,p,d);
    }
    /* Consumir mejora temporal de magia (si existía por algún evento que no se añadió en el código base) */
    e->tempMatkMul = 1;
    printf("========================================\n");
}

/* Dar 3 magia aleatoria a enemigos (variedad). Se excluye por
   defecto el Dado del Destino para evitar muertes instantáneas frecuentes. */
static void give_enemy_magic(Character *e) {
    /* Definiciones de TODAS las magias disponibles para enemigos (excluyendo Dado del Destino) */
    int (*spell_choices[4])(void*, void*, int) = { spell_fireball, spell_frost, spell_holy_blade, spell_shadow_bolt };
    const char *names[4] = { "Bola de Fuego", "Escarcha", "Espada de Luz", "Rayo Sombrio" };
    
    /* Necesitamos 3 magias, pero solo hay 4 disponibles para elegir */
    int need = 3; 
    e->numSpells = 0; /* Reiniciar */
    
    int used[4] = {0}; /* Para evitar repetir magias */
    
    /* Bucle para seleccionar las 3 magias */
    while (e->numSpells < need) {
        int idx = rand() % 4; /* Elegir entre las 4 disponibles */
        
        /* Comprueba si el hechizo en el índice 'idx' NUNCA ha sido seleccionado antes (usando la bandera 'used') */
        if (!used[idx]) {
            used[idx] = 1;  /* Si no ha sido usado: marca la bandera como '1' (usado) */
            /* Aritmética de punteros para asignación */
            /* e->spells + e->numSpells es la dirección donde guardar el nuevo puntero a función */
            *(e->spells + e->numSpells) = spell_choices[idx]; 
            *(e->spellNames + e->numSpells) = names[idx];
            e->numSpells++;
        }
    }
}

/* Combate entre dos personajes: el jugador actúa primero; el bucle repite
   player_turn y enemy_turn hasta que uno muera. Devuelve números de turnos.
   - Antes de empezar se da magia al enemigo si procede.
   - Se maneja el "truco" de evasión (revertir DEF) verificando player->def>200 */
static int battle(Character *player, Character *enemy, Item **inventory, int *invCount) {
    printf("\n==== Batalla contra %s ====\n", enemy->name);
    int turns = 0;
    give_enemy_magic(enemy);
    while (player->hp>0 && enemy->hp>0) {
        turns++;
        player_turn(player, enemy, inventory, invCount);
        if (enemy->hp<=0) break;
        enemy_turn(enemy, player);
        /* Revertir posible evasión: bajar la DEF gigante si se puso */
        if (player->def>200) player->def -= 1000;
    }
    if (player->hp>0) printf("\nHas vencido a %s en %d turno(s)!\n", enemy->name, turns);
    else printf("\n%s ha sido derrotado por %s...\n", player->name, enemy->name);
    return turns;
}

/* Jefe secreto: se crea una estructura especial muy poderosa. */
static Character *create_secret_boss(void) {
    Character *s = (Character*)malloc(sizeof(Character));
    *s = (Character){
        "EL OBSERVADOR ETERNO", 120,120, 20,20, 22,22, 0, DMG_FIRE, 0,
        0, {0}, {0}, 1, 1, 0
    };
    return s;
}

/* Función para leer una elección numérica de forma más segura */
static int read_choice(int max_val) {
    int choice = -1;
    char line[64];
    /* Lee una línea completa, lo que ayuda a limpiar el buffer de entrada */
    if (fgets(line, sizeof(line), stdin)) {
        if (sscanf(line, "%d", &choice) != 1) {
            choice = -1; /* No se pudo leer un entero, invalido */
        }
    }
    /* Valida el rango (0 es una opción para "descartar") */
    if (choice < 0 || choice > max_val) return -1;
    return choice;
}

/* Función para manejar la recompensa de objects con intercambio de inventario */
static void handle_rewards(Character *player, Item **inventory, int *invCount) {
    printf("\n=== RECOMPENSAS ===\n");
    printf("Victoria! Recibes 2 objetos magicos.\n");

    /* Generar 4 objects aleatorios */
    Item *objects[4]; /* Array local de punteros para los 4 drops */
    
    /* Aritmética de punteros para generar recompensas */
    Item **objGenPtr = objects;
    for (int i = 0; i < 4; i++) {
        *objGenPtr = crear_objeto_aleatorio();
        if (!*objGenPtr) {
            fprintf(stderr, "Error: No se pudo crear objeto de recompensa.\n");
            /* Limpieza de memoria */
            for (int j = 0; j < i; j++) free(objects[j]);
            return;
        }
        objGenPtr++; /* Avanza el puntero a la siguiente Item* en el arreglo objects */
    }
    
    int rewards_to_process = 2;
    int processed_count = 0;
    
    while (processed_count < rewards_to_process) {
        printf("\n--- Objeto de Recompensa %d de %d ---\n", processed_count + 1, rewards_to_process);

        /* Mostrar solo los objetos que no han sido seleccionados (no son NULL) */
        printf("\nOpciones de Recompensa (elige el numero o 0 para descartar TODA la recompensa actual):\n");
        int available_count = 0;
        /* Aritmética de punteros para mostrar opciones */
        Item **objShowPtr = objects;
        for (int i = 0; i < 4; i++) {
            if (*objShowPtr != NULL) {
                printf("[%d] ", i + 1);
                mostrar_objeto_detallado(*objShowPtr);
                available_count++;
            }
            objShowPtr++; /* Avanza el puntero a la siguiente Item* */
        }
        
        if (available_count == 0 && rewards_to_process == 2) {
             printf("Error: No quedan objetos de recompensa disponibles para seleccionar.\n");
             /* Si rewards_to_process es 2 y ya no hay disponibles, terminamos. */
             break;
        }


        printf("Tu eleccion (1-4, 0 para descartar recompensa %d): ", processed_count + 1);
        int reward_pick = read_choice(4);

        if (reward_pick == 0) {
            printf("Has decidido descartar esta oportunidad de recompensa.\n");
            processed_count++;
            continue;
        }
        
        /* Validar que la elección sea de un objeto disponible */
        if (reward_pick < 1 || reward_pick > 4 || *(objects + (reward_pick - 1)) == NULL) {
             printf("Eleccion invalida o el objeto ya fue tomado. Intenta de nuevo.\n");
             continue; /* Hace el ciclo de nuevo */
        }

        int reward_idx = reward_pick - 1;
        /* Aritmética de punteros para seleccionar item */
        Item *new_object = *(objects + reward_idx); /* Acceso al objeto por aritmética */
        
        if (*invCount < MAX_INV) {
            /* Inventario no lleno: añadir directamente */
            *(inventory + *invCount) = new_object; /* Añadir a inventario por aritmética */
            (*invCount)++;
            printf("Objeto '%s' anadido al inventario. (%d/%d).\n", new_object->nombre, *invCount, MAX_INV);
            *(objects + reward_idx) = NULL; /* Marcar como tomado (transferido) */
            processed_count++;
        } else {
            /* Inventario lleno: ofrecer reemplazo o descarte (cumpliendo el requisito del usuario) */
            int replace_option = -1;
            
            printf("\n========================================\n");
            printf("Inventario lleno! (%d/%d).\n", MAX_INV, MAX_INV);
            printf("Objeto de recompensa: "); mostrar_objeto_detallado(new_object);
            
            show_inventory(inventory, *invCount);
            
            printf("\nQue deseas hacer con el nuevo objeto ('%s')?\n", new_object->nombre);
            printf(" 0) Descartar el nuevo objeto.\n");
            printf(" 1-%d) Reemplazar un objeto de tu inventario (elige el numero a reemplazar).\n> ", MAX_INV);
            
            replace_option = read_choice(MAX_INV);
            
            if (replace_option >= 1 && replace_option <= MAX_INV) {
                /* Reemplazar objeto */
                int inv_idx = replace_option - 1;
                
                /* Liberar el objeto antiguo */
                Item *old_object = *(inventory + inv_idx); /* Acceso al objeto antiguo por aritmética */
                printf("Has liberado '%s'.\n", old_object->nombre);
                free(old_object);
                
                /* Colocar el nuevo objeto en su lugar */
                *(inventory + inv_idx) = new_object; /* Reemplazo por aritmética */
                printf("Objeto '%s' anadido al inventario en reemplazo.\n", new_object->nombre);
                *(objects + reward_idx) = NULL; /* Marcar como tomado (transferido) */
                processed_count++;
            } else if (replace_option == 0) {
                /* Descartar el nuevo objeto (y liberarlo) */
                printf("Has descartado el objeto de recompensa: '%s'.\n", new_object->nombre);
                free(new_object);
                *(objects + reward_idx) = NULL; /* Marcar como liberado/descartado */
                processed_count++;
            } else {
                /* Opción inválida que no es 0 */
                printf("Opcion invalida. El objeto de recompensa ('%s') ha sido descartado.\n", new_object->nombre);
                free(new_object);
                *(objects + reward_idx) = NULL; /* Marcar como liberado/descartado */
                processed_count++;
            }
        }
    }
    
    /* Liberar los objetos restantes de la generación que no fueron tomados */
    /* Aritmética de punteros para limpiar memoria */
    Item **objCleanPtr = objects;
    for (int i = 0; i < 4; i++) {
        if (*objCleanPtr != NULL) {
            free(*objCleanPtr);
        }
        objCleanPtr++;
    }
    // -------------------------------------------------------------
}

/* --------------------------- MAIN ---------------------- */
int main(void) {
    srand((unsigned)time(NULL));
    
    /* Título ASCII art del juego (idea de Fernandez jajaja) */
    printf("  _____ __              __                            ____   ________                  _ __       \n");
    printf(" / ___// /_  ____ _____/ /___ _      _______   ____  / __/  / ____/ /____  _________  (_) /___  __\n");
    printf(" \\__ \\/ __ \\/ __ `/ __  / __ \\ | /| / / ___/  / __ \\/ /_   / __/ / __/ _ \\/ ___/ __ \\/ / __/ / / /\n");
    printf(" ___/ / / / / /_/ / /_/ / /_/ / |/ |/ (__  )  / /_/ / __/  / /___/ /_/  __/ /  / / / / / /_/ /_/ / \n");
    printf("/____/_/ /_/\\__,_/\\__,_/\\____/|__/|__/____/   \\____/_/    /_____/\\__/\\___/_/  /_/ /_/_/\\__/\\__, /  \n");
    printf("                                                                                          /____/   \n");
    printf("==================================================================================================\n");
    printf("========================================\n");
    printf("POR: DIEGO ESTEVEZ & BRAULIO MILANES\n");
    printf("========================================\n");

    Character *player = create_player();

    /* Inventario dinámico: array de punteros a Item. Se inicializa con calloc
       para tener punteros NULL si no hay objects. */
    Item **inventory = (Item**)calloc(MAX_INV, sizeof(Item*));
    int invCount = 0; /* inicia sin objects */

    /* Construir templates de enemigos (plantillas) */
    EnemyTemplate easy[5], mid[5], hard[5], boss[5];
    build_enemy(easy, mid, hard, boss);

    int win = 1; /* bandera si el jugador sigue vivo */
    int totalTurnsToFinal = 0;

    /* Tres batallas de progresión: easy -> mid -> hard */
    EnemyTemplate *tiers[3] = { easy, mid, hard };
    for (int i=0;i<3 && win;i++) {
        printf("\n== CAPITULO %d ==\n", i+1);
        int d = roll_d20(player->luck);
        printf("Tiraste el dado=%d para elegir enemigo.\n", d);
        Character *enemy = pick_enemy(tiers[i], d);
        if (!enemy) { fprintf(stderr, "Error: No se pudo crear enemigo.\n"); win = 0; break; }

        printf("Te enfrentas a: %s\n", enemy->name);
        int usedTurns = battle(player, enemy, inventory, &invCount);
        free(enemy); /* liberar enemigo creado dinámicamente */
        if (player->hp<=0) win = 0; else {
            totalTurnsToFinal += usedTurns;
            
            /* Usa la función handle_rewards para poder manejar los objetos de mejor manera */
            handle_rewards(player, inventory, &invCount);
            
        }
    }

    /* Si se llega al final: Jefe final */
    if (win) {
        printf("\n== JEFE FINAL ==\nLanza d20 para determinar al jefe... Pulsa Enter..."); getchar();
        int d = roll_d20(player->luck);
        printf("Tiraste el dado=%d para elegir Jefe Final.\n", d);
        Character *finalBoss = pick_enemy(boss, d);
        if (!finalBoss) { fprintf(stderr, "Error: No se pudo crear jefe final.\n"); win = 0; }
        
        if (win) {
            printf("Jefe seleccionado: %s\n", finalBoss->name);
            int turns = battle(player, finalBoss, inventory, &invCount);
            free(finalBoss);
            if (player->hp<=0) win = 0; else totalTurnsToFinal += turns;
            
            /* Condición de jefe secreto: vencer al final en <3 turnos */
            if (win && turns < 3) {
                printf("\nHas despertado al JEFE SECRETO! Solo el 'Dado del Destino' con d20=20 puede vencerlo de un golpe.\n");
                Character *secret = create_secret_boss();
                if (!secret) { fprintf(stderr, "Error: No se pudo crear jefe secreto.\n"); }
                else {
                    int sTurns = battle(player, secret, inventory, &invCount);
                    free(secret);
                    (void)sTurns;
                    if (player->hp<=0) win=0;
                }
            }
        }
    }

    if (win) printf("\nFelicitaciones %s! Has completado la aventura.\n", player->name);
    else printf("\nLa aventura termina aqui.\n");

    /* Liberar memoria: inventario y jugador */
    /* Aritmética de punteros para liberar inventario */
    Item **invFreePtr = inventory;
    for (int i=0; i<invCount; i++) {
        free(*invFreePtr);
        invFreePtr++;
    }
    free(inventory);
    free(player);
    return 0;
}
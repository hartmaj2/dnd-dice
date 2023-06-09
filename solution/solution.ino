#include "funshield.h"

/*
 * When buttons are asked what they are currently sensing using `get_signal(unsigned long time)` they can return these signals:
 * 1. PRESSED - the button was just pressed and is held until we get the `RELEASED` signal
 * 2. RELEASED - the button was just released and will be up and returning `NOTHING` until we press it again and get the `PRESSED` signal
 * 3. NOTHING - the button is currently up until we press it, when that happens, it returns the `PRESSED` signal
 */
enum Button_signal {RELEASED,PRESSED,NOTHING};

/*
 *  The program can be in these states:
 *  1. CONFIG - allows you to choose the type and amounts of dice, when going from CONFIG to IDLE, we can see the last result (which was saved)
 *  2. IDLE - shows your last rolled number
 *  3. GENERATING - the roll button is held and the program is making a rolling animation (different possible results appear in sequence)
 */
enum Program_state {CONFIG,IDLE,GENERATING};

// INITIAL GLOBAL PROGRAM VALUES
constexpr int dice_types[] = {4,6,8,10,12,20,100};
constexpr int dice_types_length = sizeof(dice_types) / sizeof(dice_types[0]);

constexpr int roll_button_index = 0;
constexpr int amount_button_index = 1;
constexpr int type_button_index = 2;

constexpr Program_state start_state = CONFIG;

// GLOBAL VARIABLES
Program_state current_state;

class Dice
{
  private:
    static constexpr int max_dice_amount = 9;
    static constexpr int start_dice_type_index = 1;
    static constexpr int start_dice_amount = 1;
    static constexpr int clear_memory_value = -1;
    
    unsigned long roll_start_time;

    int dice[max_dice_amount];
    int current_dice_amount;
    int current_type_index;

    int last_roll;

    bool is_first_roll;

    int get_dice_sum()
    {
      int sum = 0;
      for (int i = 0; i < current_dice_amount; ++i)
      {
        sum += dice[i];
      }
      return sum;
    }

    void refresh_all_dice()
    {
        for (int i = 0; i < current_dice_amount; ++i)
        {
          dice[i] = random(dice_types[current_type_index]) + 1;
        }
    }

  public:
    void init()
    {
      roll_start_time = 0;
      current_type_index = start_dice_type_index;
      current_dice_amount = start_dice_amount;
      last_roll = clear_memory_value;
      is_first_roll = true;
    }

    /*
     * Used in conjunction with `finish_roll(int time_now)` method if we want to take the time the button was held into account
     */
    void start_roll(unsigned long time_now)
    {
      roll_start_time = time_now;
    }

    /*
     * This method used to take the hold time of the button into account but was switched to a simple version
     */
    int finish_roll(unsigned long time_now)
    {
      if (is_first_roll)
      {
        is_first_roll = false;
        unsigned long diff = time_now - roll_start_time;
        randomSeed(diff + time_now);
      }
      refresh_all_dice();
      last_roll = get_dice_sum();
      return last_roll;
      
    }

    void next_dice_type()
    {
      current_type_index++;
      current_type_index %= dice_types_length;
    }

    int get_dice_type()
    {
      return dice_types[current_type_index];
    }

    int get_max_digits_amount()
    {
      int max_dice_value = dice_types[current_type_index] * current_dice_amount;

      int digits_amount = 0;
      while (max_dice_value > 0)
      {
        digits_amount++;
        max_dice_value /= 10;
      }

      return digits_amount;
    }

    void next_dice_amount()
    {
      current_dice_amount++;
      if (current_dice_amount > max_dice_amount)
      {
        current_dice_amount = 1;
      }
    }

    int get_dice_amount()
    {
      return current_dice_amount;
    }

    void reset_roll_memory()
    {
      last_roll = clear_memory_value;
    }

    /*
     * Returns true if we havent rolled so far
     */
    bool has_roll_in_memory()
    {
      return last_roll != clear_memory_value;
    }

    int get_last_roll()
    {
      return last_roll;
    }

};

class Button 
{

  private:
    static constexpr unsigned long debounce_time = 5;

    static constexpr int button_pins[]{ button1_pin, button2_pin, button3_pin };
    static constexpr int button_pins_count = sizeof(button_pins) / sizeof(button_pins[0]);

    bool debouncing;
    bool down;
    int pin;
    unsigned long last_event;

    bool is_pressed() 
    {
      return !digitalRead(pin);
    }

  public:
    void init(int index)
    {
      debouncing = false;
      down = false;
      pinMode(button_pins[index], INPUT);
      pin = button_pins[index];
      last_event = 0;
    }

    Button_signal get_signal(unsigned long time_now) 
    {
      if (debouncing) {
        unsigned long time_diff = time_now - last_event;
        if (time_diff >= debounce_time) {
          debouncing = false;
        }
        return NOTHING;
      } else {
        if (down) {
          if (!is_pressed()) {
            debouncing = true;
            down = false;
            last_event = time_now;
            return RELEASED;
          }
          return NOTHING;
        } else {
          if (is_pressed()) {
            debouncing = true;
            down = true;
            last_event = time_now;
            return PRESSED;
          }
          return NOTHING;
        }
      }
    }

};

/*
 * Detects if certain interval elapsed. It is needed to actively check using the `should_signal(unsigned long time_now)` method
 */
class Timer
{
  private:
    unsigned long delay;
    unsigned long last_event;

  public:
    void init(unsigned long init_time, unsigned long init_delay)
    {
      last_event = init_time;
      delay = init_delay;
    }

    bool should_signal(unsigned long time_now)
    {
      unsigned long diff = time_now - last_event;
      if (diff >= delay)
      {
        last_event += delay;
        return true;
      }
      return false;
    }

    /*
     * This is needed for the animation. If we start new animation, we don't want to have the old `last_time` value.
     * That would result in `should_signal(unsigned long time_now)` method having to catch up with the current time
     */
    void reset_last_event_time(unsigned long time_now)
    {
      last_event = time_now;
    }

};

/*
 * Takes care of displaying and multiplexing roll results, dice config strings and the rolling animation.
 */
class Display
{
  private:
    // special symbols
    static constexpr byte empty_glyph = 0b11111111;
    static constexpr byte d_glyph = 0b10100001;  

    // animation boundaries
    static constexpr byte animation_start_frame = 0x80;
    static constexpr byte animation_end_frame = 0x04;

    // display parameters
    static constexpr int display_digits = 4;
    static constexpr int dice_type_digits = 2; // amount of reserved digits for the dice type when in config mode
    static constexpr int dice_amount_digits = 1; // same as above but for dice amount

    // timing settings
    static constexpr unsigned long multiplex_delay = 1;
    static constexpr unsigned long animation_delay = 50;

    Timer multiplex_timer;
    Timer animation_timer;

    byte current_animation_frame; 

    int current_active_index;
    
    char display_buffer[display_digits+1]; // tells the display what characters should be written out and multiplexed (serves as a display buffer)

    /*
     * Writes next number in sequence to the `display_buffer`
     * (for example config string has two different numbers to be written, one for the amount and one for the type)
     */
    void write_next_number(int number, int &index, int digit_limit)
    {
      for (int i = 0; i < digit_limit; ++i)
      {
        
        int remainder = number % 10;
        display_buffer[index] = (char) (remainder + '0');
        index--;
        number = number / 10;
        if (number < 1) break;
      }
    }

    /*
     * Outputs given character on display
     */
    void output_char(char ch, byte pos)
    {
      byte glyph = empty_glyph;
      if (ch == 'd')
      {
        glyph = d_glyph;
      }
      else if (ch >= '0' && ch <= '9')
      {
        glyph = digits[(int) (ch - '0')];
      }
      digitalWrite(latch_pin, LOW);
      shiftOut(data_pin, clock_pin, MSBFIRST, glyph);
      shiftOut(data_pin, clock_pin, MSBFIRST, 1 << pos);
      digitalWrite(latch_pin, HIGH);
    }

    /*
     * Erases leftover characters from the display buffer
     */
    void erase_others(int index)
    {
      
      for (int i = index; i >= 0; --i)
      {
        display_buffer[i] = ' ';
      }
      
    }

    /*
     * Clears the display buffer
     */
    void erase_all()
    {
      for (int i = 0; i < display_digits; ++i)
      {
        display_buffer[i] = ' ';
      }
    }

    /*
     * Updates the current active index used for multiplexing
     */
    void update_active_index(unsigned long time_now)
    {
      if (multiplex_timer.should_signal(time_now))
      {
        current_active_index = (current_active_index + 1) % display_digits;
      }
    }

    /*
     * Outputs the `current_animation_frame` to display at the `current_active_index` position
     */
    void output_animation_frame()
    {
      digitalWrite(latch_pin, LOW);
      shiftOut(data_pin, clock_pin, LSBFIRST, ~current_animation_frame);
      shiftOut(data_pin, clock_pin, LSBFIRST, 1 << (current_active_index + 4) );
      digitalWrite(latch_pin, HIGH);
    }

  public:
    void init(unsigned long init_time)
    {
      pinMode(latch_pin, OUTPUT);
      pinMode(clock_pin, OUTPUT);
      pinMode(data_pin, OUTPUT);
      multiplex_timer.init(init_time,multiplex_delay);
      animation_timer.init(init_time,animation_delay);
      current_animation_frame = animation_start_frame;
    }

    /*
     * Updates the `current_animation_frame` value so that it stays within the boundaries
     */
    void next_animation()
    {
      current_animation_frame = current_animation_frame >> 1;
      if (current_animation_frame < animation_end_frame)
      {
        current_animation_frame = animation_start_frame;
      }
    }

    /*
     * The animation is only done at digits that the maximum possible dice throw for the current configuration will occupy 
     */
    void multiplex_animation(unsigned long time_now, int max_digits_amount)
    {
      update_active_index(time_now);

      if (current_active_index >= max_digits_amount)
      {
        current_active_index = 0;
      }

      output_animation_frame();
    }

    void multiplex(unsigned long time_now)
    {
      update_active_index(time_now);
      output_char(display_buffer[current_active_index],current_active_index);
    }

    void write_number(int number)
    {

      int index = display_digits - 1;
      write_next_number(number, index, display_digits);
      erase_others(index);
      
    }

    void write_config_string(int amount, int type)
    {
      int index = display_digits - 1;
      write_next_number(type, index, dice_type_digits);
      display_buffer[index] = 'd';
      index--;
      write_next_number(amount, index, dice_amount_digits);
      erase_others(index);
   
    }

    /*
     * Resets the timer and the animation frame
     */
    void prepare_animation(unsigned long time_now)
    {
      current_animation_frame = animation_start_frame;
      animation_timer.reset_last_event_time(time_now);
    }

    bool should_animate(unsigned long time_now)
    {
      if (animation_timer.should_signal(time_now))
      {
        return true;
      }
      return false;
    }

};

Button roll_button;
Button type_button;
Button amount_button;
Display disp;
Dice dice;

void init_buttons()
{
  roll_button.init(roll_button_index);
  amount_button.init(amount_button_index);
  type_button.init(type_button_index);
}

void init_display(unsigned long time_now, Dice dice)
{

  pinMode(latch_pin, OUTPUT);
  pinMode(clock_pin, OUTPUT);
  pinMode(data_pin, OUTPUT);

  disp.init(time_now);

  disp.write_config_string(dice.get_dice_amount(), dice.get_dice_type());
  
}

void setup() 
{

  //Serial.begin(9600);

  unsigned long time_now = millis();

  current_state = start_state;
  
  init_buttons();

  dice.init();
  
  init_display(time_now, dice);

}

void loop() 
{
  unsigned long time_now = millis();

  // First we get signals from all the dice
  Button_signal roll_signal = roll_button.get_signal(time_now);
  Button_signal type_signal = type_button.get_signal(time_now);
  Button_signal amount_signal = amount_button.get_signal(time_now);

  // Now we use the current state and the signals we got from the dice to decide what to do
  switch (current_state)
  {
    case IDLE :

      // IDLE -> GENERATING
      if (roll_signal == PRESSED || !dice.has_roll_in_memory())
      {
        dice.start_roll(time_now);
        disp.prepare_animation(time_now);
        disp.next_animation();
        current_state = GENERATING; 
      }

      // IDLE -> CONFIG
      if (type_signal == PRESSED || amount_signal == PRESSED)
      {
        disp.write_config_string(dice.get_dice_amount(), dice.get_dice_type());
        current_state = CONFIG; 
      }
    break;

    case CONFIG :

      // change dice type
      if (type_signal == PRESSED)
      {
        dice.reset_roll_memory();
        dice.next_dice_type();
        disp.write_config_string(dice.get_dice_amount(), dice.get_dice_type());
      }

      // change dice amount
      if (amount_signal == PRESSED)
      {
        dice.reset_roll_memory();
        dice.next_dice_amount();
        disp.write_config_string(dice.get_dice_amount(), dice.get_dice_type());
      }

      // CONFIG -> IDLE (shows the last roll result)
      if (roll_signal == PRESSED)
      {
        disp.write_number(dice.get_last_roll());
        current_state = IDLE;
      }
    break;

    case GENERATING :

      // GENERATING -> IDLE
      if (roll_signal == RELEASED)
      {
        disp.write_number(dice.finish_roll(time_now));
        current_state = IDLE;
      }

      // we are currently generating the roll
      else {
        if (disp.should_animate(time_now))
        {
          disp.next_animation();
        }
      }
    break;

  }

  /*
   * Multiplexing behaves differently in the GENERATING mode which is reserved for the animation
   */
  if (current_state == GENERATING)
  {
  disp.multiplex_animation(time_now,dice.get_max_digits_amount());
  }
  else
  {
    disp.multiplex(time_now);
  }
  
  
}

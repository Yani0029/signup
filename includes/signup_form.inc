<?php
/**
 * @file
 * Code for the form when users sign up.
 */

/**
 * Build the user signup form.
 *
 * @param stdClass $node
 *   The fully loaded node object.
 * @param string $signup_type
 *   Determines what kind of signup to generate a form for. Possible values:
 *    'auth' -- regular authenticated user signup form
 *    'anon' -- anonymous user signup form (includes required email field).
 *    'admin' -- admin form to signup another user (includes user selector).
 * @param bool $fieldset
 *   Boolean that indicates if the signup form should be in a fieldset.
 *
 * @return array
 *   An FAPI array defining the signup form.
 */
function signup_form($form, &$form_state, $node, $signup_type = 'auth', $fieldset = TRUE) {
  global $user;

  $form['nid'] = array(
    '#type' => 'value',
    '#value' => $node->nid,
  );
  $form['uid'] = array(
    '#type' => 'value',
    '#value' => $user->uid,
  );

  if ($fieldset) {
    $form['collapse'] = array(
      '#type' => 'fieldset',
      '#collapsible' => TRUE,
      '#collapsed' => variable_get('signup_fieldset_collapsed', 1),
    );
    if ($signup_type == 'admin') {
      $form['collapse']['#title'] = t('Sign up another user');
      // We always want this fieldset expanded on the node/N/signups tab.
      $form['collapse']['#collapsed'] = FALSE;
    }
    else {
      $form['collapse']['#title'] = t('Sign up for @title', array('@title' => $node->title));
    }
  }
  else {
    $form['collapse'] = array();
  }

  $signup_form = array();
  if ($signup_type == 'anon') {
    $anon_form = array();
    $anon_form['signup_anon_mail'] = array(
      '#type' => 'textfield',
      '#title' => t('Email'),
      '#description' => t('An e-mail address is required for users who are not registered at this site. If you are a registered user at this site, please !login to sign up for this %node_type.', array('!login' => l(t('login'), 'user/login', array('query' => drupal_get_destination())), '%node_type' => node_type_get_name($node->type))),
      '#size' => 40,
      '#maxlength' => 255,
      '#required' => TRUE,
    );
    $validate_handler = 'signup_form_validate_anon';
    $signup_form += $anon_form;
  }
  elseif ($signup_type == 'admin') {
    $admin_form = array();
    $admin_form['signup_username'] = array(
      '#title' => t('Username'),
      '#type' => 'textfield',
      '#autocomplete_path' => 'user/autocomplete',
      '#maxlength' => USERNAME_MAX_LENGTH,
      '#size' => 40,
      '#weight' => -1,
      '#required' => TRUE,
    );
    $validate_handler = 'signup_form_validate_username';
    $signup_form += $admin_form;
  }

  // Build the themed signup form for this site and include that.
  $signup_themed_form = theme('signup_user_form', array('node' => $node));

  if ($signup_type == 'admin') {
    // Special case hack for the default signup form, where the current
    // username is being filled in as the default for the 'Name' field.
    if (!empty($signup_themed_form['signup_form_data']['Name']['#default_value'])) {
      unset($signup_themed_form['signup_form_data']['Name']['#default_value']);
    }
  }
  $signup_form += $signup_themed_form;
 // if ($signup_type == 'anon') {
    $form['collapse']['signup_user_form'] = $signup_form;
    $form['collapse']['submit'] = array(
      '#type' => 'submit',
      '#value' => t('Sign up'),
    );
    if (!empty($validate_handler)) {
    $form['#validate'][] = $validate_handler;
  }
  return $form;
}

/**
 * Submit handler for the user signup form.
 *
 * @param array $form
 *   The form being submitted.
 * @param array $form_values
 *   The state of the form, including the submitted values.
 */
function signup_form_submit($form, &$form_state) {
  if (isset($form_state['values']['signup_username'])) {
    $accounts = user_load_multiple(array(), array('name' => $form_state['values']['signup_username']));
    foreach ($accounts as $account) {
      $form_state['values']['uid'] = $account->uid;
    }
  }
  signup_sign_up_user($form_state['values']);
}

/**
 * Validate handler for the email address on the anonymous user signup form.
 *
 * @param array $form
 *   Form array for the anonymous user email field.
 * @param array $form_state
 *   State of the form, including the submitted values to validate.
 */
function signup_form_validate_anon($form, $form_state) {
  $nid = $form_state['values']['nid'];
  $anon_mail = $form_state['values']['signup_anon_mail'];
  signup_validate_anon_email($nid, $anon_mail, 'signup_anon_mail');
}

/**
 * Validate the email address for an anonymous signup.
 *
 * @param int $nid
 *   The node the user is signing up for.
 * @param string $anon_mail
 *   The anonymous email address to validate.
 * @param string|false $name
 *   The form element being validated (optional).
 *
 * @return Boolean.
 *   TRUE if the address validates, FALSE otherwise.
 */
function signup_validate_anon_email($nid, $anon_mail, $name = FALSE) {
  if (!valid_email_address($anon_mail)) {
    $message = t('Invalid email address entered for signup.');
  }
  elseif ((bool) db_query_range("SELECT 1 FROM {users} WHERE mail = :mail", 0, 1, array(':mail' => $anon_mail))->fetchField()) {
    $message = t('The email address entered belongs to a registered user.');
  }
  elseif ((bool) db_query_range("SELECT 1 FROM {signup_log} WHERE anon_mail = :mail AND nid = :nid", 0, 1, array(':mail' => $anon_mail, ':nid' => $nid))->fetchField()) {
    $node = node_load($nid);
    $message = t('The email address entered has already been used to sign up for this %node_type.', array('%node_type' => node_type_get_name($node->type)));
  }

  // If there's no message, it's a valid email, so return success.
  if (!isset($message)) {
    return TRUE;
  }

  // Depending on how we were called, propagate the error accordinly.
  if ($name) {
    form_set_error($name, $message);
  }
  else {
    drupal_set_message($message, 'error');
  }
  return FALSE;
}

/**
 * Validates the username on the admin form to signup another user.
 *
 * @param array $form
 *   Form array for the username field.
 * @param array $form_state
 *   The form state array containing node ID of the node the user is being signed up for.
 */
function signup_form_validate_username($form, $form_state) {
  $nid = $form_state['values']['nid'];
  $username = $form_state['values']['signup_username'];
  $accounts = user_load_multiple(array(), array('name' => $username));
  foreach ($accounts as $account) {
    if (empty($account)) {
      form_set_error('signup_username', t('User %user_name does not exist.', array('%user_name' => $username)));
    }
    elseif ((bool) db_query_range("SELECT 1 FROM {signup_log} WHERE uid = :uid AND nid = :nid", 0, 1, array(':uid' => $account->uid, ':nid' => $nid))->fetchField() > 0) {
      $node = node_load($nid);
      form_set_error('signup_username', t('User !user is already signed up for %title', array('!user' => theme('username', array('account' => $account)), '%title' => $node->title)));
    }
  }
}

/**
 * @todo Please document this function.
 * @see http://drupal.org/node/1354
 */
function signup_node_admin_add_user_page($node) {

  drupal_set_title($node->title);

  $output = '';
  if ($node->signup_status) {
    $output = drupal_get_form('signup_form', $node, 'admin', FALSE);
  }
  return $output;
}


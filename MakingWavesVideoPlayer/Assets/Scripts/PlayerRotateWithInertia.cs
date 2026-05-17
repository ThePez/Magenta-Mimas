using UnityEngine;

public class PlayerRotateWithInertia : MonoBehaviour
{
    public float Velocity { get; private set; }

    void Update()
    {
        if (Input.GetKey(KeyCode.A))
        {
            Velocity = -20f;
        }
        else if (Input.GetKey(KeyCode.S))
        {
            Velocity = -10f;
        }
        else if (Input.GetKey(KeyCode.D))
        {
            Velocity = -5f;
        }
        else if (Input.GetKey(KeyCode.F))
        {
            Velocity = -2.5f;
        }
        else if (Input.GetKey(KeyCode.G))
        {
            Velocity = 2.5f;
        }
        else if (Input.GetKey(KeyCode.H))
        {
            Velocity = 5f;
        }
        else if (Input.GetKey(KeyCode.J))
        {
            Velocity = 10f;
        }
        else if (Input.GetKey(KeyCode.K))
        {
            Velocity = 20f;
        }
        else
        {
            Velocity = 0;
        }

        transform.Rotate(Vector3.up, Velocity * Time.deltaTime);
    }
}